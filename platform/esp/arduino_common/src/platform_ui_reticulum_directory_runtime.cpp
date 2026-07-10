#include "platform/ui/reticulum_directory_runtime.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_runtime.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <string_view>

namespace platform::ui::reticulum_directory
{
namespace
{

using ::platform::esp::arduino_common::storage::SdRuntimeFile;

constexpr const char* kConfigDir = "/trailmate/reticulum";
constexpr const char* kAnnouncesPath = "/trailmate/reticulum/announces.tsv";
constexpr const char* kAnnouncesTempPath = "/trailmate/reticulum/announces.tmp";
constexpr const char* kLxmfAddressesPath = "/trailmate/reticulum/lxmf_addresses.tsv";
constexpr const char* kLxmfAddressesTempPath = "/trailmate/reticulum/lxmf_addresses.tmp";
constexpr std::size_t kMaxDirectoryEntries = 1024;
constexpr std::size_t kMaxLineBytes = 4096;
constexpr std::size_t kMaxTsvFields = 14;
constexpr std::size_t kLineReadChunkBytes = 256;
constexpr std::size_t kPendingAnnounceDepth = 16;
constexpr std::size_t kPendingAddressDepth = 16;
constexpr std::size_t kPendingRuntimeBlobBytes = 500;
constexpr TickType_t kAsyncMutexWait = pdMS_TO_TICKS(5);
constexpr TickType_t kAsyncDebounceDelay = pdMS_TO_TICKS(2500);
constexpr TickType_t kAsyncActivePollDelay = pdMS_TO_TICKS(5000);
constexpr TickType_t kAsyncBetweenFlushDelay = pdMS_TO_TICKS(250);
constexpr TickType_t kAsyncMaintenanceClosedDelay = pdMS_TO_TICKS(15000);
constexpr uint32_t kAsyncMaintenanceStableMs = 8000;
constexpr uint32_t kAsyncDeferLogIntervalMs = 30000;
constexpr uint32_t kAsyncTaskStackBytes = 5 * 1024;
constexpr UBaseType_t kAsyncTaskPriority = tskIDLE_PRIORITY + 1;
constexpr const char* kMaintenanceDeferredMessage =
    "Reticulum directory maintenance deferred";
constexpr uint32_t kAsyncTaskCreateFailLogIntervalMs = 10000;

enum class PendingDirectoryKind : uint8_t
{
    None = 0,
    Announce,
    Address,
};

struct TsvFields
{
    std::string_view values[kMaxTsvFields];
    std::size_t count = 0;
};

struct PendingAnnounce
{
    bool occupied = false;
    uint32_t order = 0;
    AnnounceRecord record{};
    uint8_t raw_packet[kPendingRuntimeBlobBytes] = {};
    uint8_t app_data[kPendingRuntimeBlobBytes] = {};
};

struct PendingAddress
{
    bool occupied = false;
    uint32_t order = 0;
    LxmfAddressRecord record{};
};

struct AsyncDirectoryState
{
    SemaphoreHandle_t mutex = nullptr;
    QueueHandle_t signal = nullptr;
    TaskHandle_t task = nullptr;
    uint32_t next_order = 1;
    PendingAnnounce announces[kPendingAnnounceDepth]{};
    PendingAddress addresses[kPendingAddressDepth]{};
    PendingAnnounce active_announce{};
    PendingAddress active_address{};
};

AsyncDirectoryState* s_async_state = nullptr;
uint32_t s_maintenance_window_entered_ms = 0;
uint32_t s_last_maintenance_defer_log_ms = 0;
uint32_t s_last_task_create_fail_log_ms = 0;

using DirectoryIoGate = bool (*)();

Status record_announce_sync(const AnnounceRecord& record, bool require_maintenance);
Status record_lxmf_address_sync(const LxmfAddressRecord& record, bool require_maintenance);
void async_task_entry(void* context);

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void copy_view(char* out, std::size_t out_len, std::string_view text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    const std::size_t len = text.size() < out_len - 1U ? text.size() : out_len - 1U;
    if (len != 0)
    {
        std::memcpy(out, text.data(), len);
    }
    out[len] = '\0';
}

void set_status(Status& out, const char* message, const char* detail = nullptr)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

bool sd_available()
{
    return ::platform::ui::device::card_ready() &&
           ::platform::esp::arduino_common::storage::sd_card_ready();
}

bool ensure_config_dir()
{
    if (::platform::esp::arduino_common::storage::sd_exists(kConfigDir))
    {
        return ::platform::esp::arduino_common::storage::sd_is_directory(kConfigDir);
    }
    return ::platform::esp::arduino_common::storage::sd_mkdir(kConfigDir);
}

bool zero_hash(const uint8_t* hash, std::size_t len)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (hash[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void append_hex(std::string& out, const uint8_t* data, std::size_t len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!data || len == 0)
    {
        return;
    }
    out.reserve(out.size() + (len * 2U));
    for (std::size_t i = 0; i < len; ++i)
    {
        out.push_back(kHex[(data[i] >> 4U) & 0x0FU]);
        out.push_back(kHex[data[i] & 0x0FU]);
    }
}

std::string hex_text(const uint8_t* data, std::size_t len)
{
    std::string out;
    append_hex(out, data, len);
    return out;
}

uint8_t hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return static_cast<uint8_t>(ch - 'A' + 10);
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return static_cast<uint8_t>(ch - 'a' + 10);
    }
    return 0xFF;
}

bool parse_hex(std::string_view text, uint8_t* out, std::size_t out_len)
{
    if (!out || text.size() != out_len * 2U)
    {
        return false;
    }
    for (std::size_t i = 0; i < out_len; ++i)
    {
        const uint8_t hi = hex_nibble(text[i * 2U]);
        const uint8_t lo = hex_nibble(text[(i * 2U) + 1U]);
        if (hi == 0xFF || lo == 0xFF)
        {
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4U) | lo);
    }
    return true;
}

bool copy_text_app_data_display_name_from_hex(std::string_view text,
                                              char* out,
                                              std::size_t out_len)
{
    if (!out || out_len == 0 || text.empty() || (text.size() % 2U) != 0)
    {
        return false;
    }
    const std::size_t byte_count = text.size() / 2U;
    if (byte_count > 96)
    {
        return false;
    }

    std::size_t used = 0;
    bool has_visible = false;
    for (std::size_t index = 0; index < byte_count; ++index)
    {
        const uint8_t hi = hex_nibble(text[index * 2U]);
        const uint8_t lo = hex_nibble(text[(index * 2U) + 1U]);
        if (hi == 0xFF || lo == 0xFF)
        {
            out[0] = '\0';
            return false;
        }
        uint8_t byte = static_cast<uint8_t>((hi << 4U) | lo);
        if (byte == '\t' || byte == '\r' || byte == '\n')
        {
            byte = ' ';
        }
        else if (byte == 0 || byte < 0x20 || byte == 0x7F)
        {
            out[0] = '\0';
            return false;
        }
        if (used + 1U < out_len)
        {
            out[used++] = static_cast<char>(byte);
        }
        if (byte != ' ')
        {
            has_visible = true;
        }
    }
    while (used != 0 && out[used - 1U] == ' ')
    {
        --used;
    }
    out[used] = '\0';
    return has_visible && used != 0;
}

std::string sanitize_field(const char* text)
{
    std::string out = text ? text : "";
    for (char& ch : out)
    {
        if (ch == '\t' || ch == '\r' || ch == '\n')
        {
            ch = ' ';
        }
    }
    return out;
}

std::string_view trim_view(std::string_view line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                             line.back() == ' ' || line.back() == '\t'))
    {
        line.remove_suffix(1);
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
        ++start;
    }
    line.remove_prefix(start);
    return line;
}

bool hash_equal(const uint8_t* lhs, const uint8_t* rhs, std::size_t len)
{
    return lhs && rhs && std::memcmp(lhs, rhs, len) == 0;
}

class LineReader
{
  public:
    explicit LineReader(SdRuntimeFile& file)
        : file_(file)
    {
    }

    bool read_line(std::string& out)
    {
        out.clear();
        bool got = false;
        bool overflow = false;
        while (true)
        {
            if (offset_ >= available_)
            {
                available_ = file_.read_bytes(buffer_, sizeof(buffer_));
                offset_ = 0;
                if (available_ == 0)
                {
                    break;
                }
            }

            const char ch = buffer_[offset_++];
            got = true;
            if (ch == '\n')
            {
                break;
            }
            if (out.size() < kMaxLineBytes)
            {
                out.push_back(ch);
            }
            else
            {
                overflow = true;
            }
        }
        if (overflow)
        {
            out.clear();
        }
        return got;
    }

  private:
    SdRuntimeFile& file_;
    char buffer_[kLineReadChunkBytes] = {};
    std::size_t offset_ = 0;
    std::size_t available_ = 0;
};

void bind_pending_payloads(PendingAnnounce& pending)
{
    pending.record.raw_packet =
        pending.record.raw_packet_len != 0 ? pending.raw_packet : nullptr;
    pending.record.app_data =
        pending.record.app_data_len != 0 ? pending.app_data : nullptr;
}

uint32_t monotonic_ms()
{
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

bool raw_maintenance_window()
{
    return ::platform::ui::screen::is_sleeping() &&
           !::platform::ui::screen::is_saver_active();
}

bool maintenance_window()
{
    const uint32_t now = monotonic_ms();
    if (!raw_maintenance_window())
    {
        s_maintenance_window_entered_ms = 0;
        return false;
    }
    if (s_maintenance_window_entered_ms == 0)
    {
        s_maintenance_window_entered_ms = now != 0 ? now : 1;
        return false;
    }
    return now - s_maintenance_window_entered_ms >= kAsyncMaintenanceStableMs;
}

bool io_gate_open(DirectoryIoGate gate)
{
    return !gate || gate();
}

void set_maintenance_deferred(Status& out, const char* detail)
{
    out.supported = true;
    out.sd_present = sd_available();
    set_status(out, kMaintenanceDeferredMessage, detail);
}

bool maintenance_deferred(const Status& status)
{
    return std::strncmp(status.message,
                        kMaintenanceDeferredMessage,
                        sizeof(status.message)) == 0;
}

void log_maintenance_deferred(const char* stage)
{
    const uint32_t now = monotonic_ms();
    if (s_last_maintenance_defer_log_ms != 0 &&
        now - s_last_maintenance_defer_log_ms < kAsyncDeferLogIntervalMs)
    {
        return;
    }
    s_last_maintenance_defer_log_ms = now;
    std::printf("[Reticulum][Directory] flush deferred stage=%s sleeping=%d saver=%d\n",
                stage ? stage : "-",
                ::platform::ui::screen::is_sleeping() ? 1 : 0,
                ::platform::ui::screen::is_saver_active() ? 1 : 0);
}

void log_async_task_create_failed(BaseType_t rc)
{
    const uint32_t now = monotonic_ms();
    if (s_last_task_create_fail_log_ms != 0 &&
        now - s_last_task_create_fail_log_ms < kAsyncTaskCreateFailLogIntervalMs)
    {
        return;
    }
    s_last_task_create_fail_log_ms = now;
    std::printf("[Reticulum][Directory] async task_create_failed rc=%ld "
                "internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
                static_cast<long>(rc),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void log_async_worker_stack(const char* stage)
{
    const UBaseType_t free_words = uxTaskGetStackHighWaterMark(nullptr);
    std::printf("[Reticulum][Directory] async %s stack_free_words=%u stack_free_bytes=%u\n",
                stage ? stage : "-",
                static_cast<unsigned>(free_words),
                static_cast<unsigned>(free_words * sizeof(StackType_t)));
}

AsyncDirectoryState* allocate_async_state()
{
    void* storage = heap_caps_malloc_prefer(sizeof(AsyncDirectoryState),
                                            2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!storage)
    {
        storage = ::operator new(sizeof(AsyncDirectoryState), std::nothrow);
    }
    return storage ? new (storage) AsyncDirectoryState() : nullptr;
}

bool start_async_worker_task(AsyncDirectoryState& state)
{
    if (state.task)
    {
        return true;
    }
    if (!state.mutex || !state.signal)
    {
        return false;
    }

    const BaseType_t ok = xTaskCreate(async_task_entry,
                                      "rtdir_io",
                                      kAsyncTaskStackBytes,
                                      &state,
                                      kAsyncTaskPriority,
                                      &state.task);
    if (ok != pdPASS)
    {
        log_async_task_create_failed(ok);
        state.task = nullptr;
        return false;
    }
    return true;
}

AsyncDirectoryState* ensure_async_worker()
{
    if (!s_async_state)
    {
        s_async_state = allocate_async_state();
        if (s_async_state)
        {
            std::printf("[Reticulum][Directory] async state allocated bytes=%u\n",
                        static_cast<unsigned>(sizeof(AsyncDirectoryState)));
        }
    }
    AsyncDirectoryState* state = s_async_state;
    if (!state)
    {
        return nullptr;
    }
    if (!state->mutex)
    {
        state->mutex = xSemaphoreCreateMutex();
    }
    if (!state->signal)
    {
        state->signal = xQueueCreate(1, sizeof(uint8_t));
    }
    if (!state->mutex || !state->signal)
    {
        return nullptr;
    }
    (void)start_async_worker_task(*state);
    return state;
}

PendingAnnounce* find_announce_slot(AsyncDirectoryState& state,
                                    const uint8_t destination_hash[kReticulumHashSize])
{
    PendingAnnounce* empty = nullptr;
    PendingAnnounce* oldest = nullptr;
    for (auto& pending : state.announces)
    {
        if (pending.occupied &&
            hash_equal(pending.record.destination_hash, destination_hash, kReticulumHashSize))
        {
            return &pending;
        }
        if (!pending.occupied && !empty)
        {
            empty = &pending;
        }
        if (pending.occupied && (!oldest || pending.order < oldest->order))
        {
            oldest = &pending;
        }
    }
    return empty ? empty : oldest;
}

PendingAddress* find_address_slot(AsyncDirectoryState& state,
                                  const uint8_t destination_hash[kReticulumHashSize])
{
    PendingAddress* empty = nullptr;
    PendingAddress* oldest = nullptr;
    for (auto& pending : state.addresses)
    {
        if (pending.occupied &&
            hash_equal(pending.record.destination_hash, destination_hash, kReticulumHashSize))
        {
            return &pending;
        }
        if (!pending.occupied && !empty)
        {
            empty = &pending;
        }
        if (pending.occupied && (!oldest || pending.order < oldest->order))
        {
            oldest = &pending;
        }
    }
    return empty ? empty : oldest;
}

bool has_pending_locked(const AsyncDirectoryState& state)
{
    for (const auto& pending : state.announces)
    {
        if (pending.occupied)
        {
            return true;
        }
    }
    for (const auto& pending : state.addresses)
    {
        if (pending.occupied)
        {
            return true;
        }
    }
    return false;
}

bool has_pending(AsyncDirectoryState& state)
{
    if (xSemaphoreTake(state.mutex, kAsyncMutexWait) != pdTRUE)
    {
        return true;
    }
    const bool pending = has_pending_locked(state);
    xSemaphoreGive(state.mutex);
    return pending;
}

bool pop_oldest_pending(AsyncDirectoryState& state, PendingDirectoryKind* out_kind)
{
    if (!out_kind)
    {
        return false;
    }
    *out_kind = PendingDirectoryKind::None;
    if (xSemaphoreTake(state.mutex, portMAX_DELAY) != pdTRUE)
    {
        return false;
    }

    PendingAnnounce* oldest_announce = nullptr;
    PendingAddress* oldest_address = nullptr;
    for (auto& pending : state.announces)
    {
        if (pending.occupied &&
            (!oldest_announce || pending.order < oldest_announce->order))
        {
            oldest_announce = &pending;
        }
    }
    for (auto& pending : state.addresses)
    {
        if (pending.occupied &&
            (!oldest_address || pending.order < oldest_address->order))
        {
            oldest_address = &pending;
        }
    }

    if (oldest_announce &&
        (!oldest_address || oldest_announce->order <= oldest_address->order))
    {
        state.active_announce = *oldest_announce;
        bind_pending_payloads(state.active_announce);
        *oldest_announce = PendingAnnounce{};
        *out_kind = PendingDirectoryKind::Announce;
    }
    else if (oldest_address)
    {
        state.active_address = *oldest_address;
        *oldest_address = PendingAddress{};
        *out_kind = PendingDirectoryKind::Address;
    }

    xSemaphoreGive(state.mutex);
    return *out_kind != PendingDirectoryKind::None;
}

void requeue_active_announce(AsyncDirectoryState& state)
{
    if (xSemaphoreTake(state.mutex, portMAX_DELAY) != pdTRUE)
    {
        return;
    }
    PendingAnnounce* slot =
        find_announce_slot(state, state.active_announce.record.destination_hash);
    if (slot)
    {
        *slot = state.active_announce;
        slot->occupied = true;
        slot->order = state.next_order++;
        bind_pending_payloads(*slot);
    }
    xSemaphoreGive(state.mutex);
}

void requeue_active_address(AsyncDirectoryState& state)
{
    if (xSemaphoreTake(state.mutex, portMAX_DELAY) != pdTRUE)
    {
        return;
    }
    PendingAddress* slot =
        find_address_slot(state, state.active_address.record.destination_hash);
    if (slot)
    {
        *slot = state.active_address;
        slot->occupied = true;
        slot->order = state.next_order++;
    }
    xSemaphoreGive(state.mutex);
}

void signal_async_worker(AsyncDirectoryState& state)
{
    (void)start_async_worker_task(state);
    if (!state.signal)
    {
        return;
    }
    const uint8_t signal = 1;
    (void)xQueueOverwrite(state.signal, &signal);
}

void async_task_entry(void* context)
{
    auto* state = static_cast<AsyncDirectoryState*>(context);
    if (!state)
    {
        vTaskDelete(nullptr);
        return;
    }
    log_async_worker_stack("start");

    uint8_t signal = 0;
    for (;;)
    {
        const TickType_t wait_ticks =
            has_pending(*state) ? kAsyncActivePollDelay : portMAX_DELAY;
        (void)xQueueReceive(state->signal, &signal, wait_ticks);
        if (!has_pending(*state))
        {
            continue;
        }
        vTaskDelay(kAsyncDebounceDelay);
        if (!maintenance_window())
        {
            log_maintenance_deferred("window");
            vTaskDelay(kAsyncMaintenanceClosedDelay);
            continue;
        }

        PendingDirectoryKind kind = PendingDirectoryKind::None;
        if (!pop_oldest_pending(*state, &kind))
        {
            continue;
        }
        if (!maintenance_window())
        {
            log_maintenance_deferred("pre_flush");
            if (kind == PendingDirectoryKind::Announce)
            {
                requeue_active_announce(*state);
            }
            else if (kind == PendingDirectoryKind::Address)
            {
                requeue_active_address(*state);
            }
            vTaskDelay(kAsyncMaintenanceClosedDelay);
            continue;
        }

        Status status{};
        if (kind == PendingDirectoryKind::Announce)
        {
            bind_pending_payloads(state->active_announce);
            status = record_announce_sync(state->active_announce.record, true);
            if (status.sd_present && !status.saved)
            {
                if (maintenance_deferred(status))
                {
                    log_maintenance_deferred("announce_write");
                }
                else
                {
                    std::printf("[Reticulum][Directory] announce_flush failed message=%s detail=%s\n",
                                status.message,
                                status.detail);
                }
                requeue_active_announce(*state);
            }
        }
        else if (kind == PendingDirectoryKind::Address)
        {
            status = record_lxmf_address_sync(state->active_address.record, true);
            if (status.sd_present && !status.saved)
            {
                if (maintenance_deferred(status))
                {
                    log_maintenance_deferred("address_write");
                }
                else
                {
                    std::printf("[Reticulum][Directory] address_flush failed message=%s detail=%s\n",
                                status.message,
                                status.detail);
                }
                requeue_active_address(*state);
            }
        }

        vTaskDelay(kAsyncBetweenFlushDelay);
        if (has_pending(*state))
        {
            signal_async_worker(*state);
        }
    }
}

Status queue_announce_async(const AnnounceRecord& record)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid Reticulum announce", kAnnouncesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kAnnouncesPath);
        return out;
    }

    AsyncDirectoryState* state = ensure_async_worker();
    if (!state)
    {
        set_status(out, "Reticulum directory queue unavailable", kAnnouncesPath);
        return out;
    }
    if (xSemaphoreTake(state->mutex, kAsyncMutexWait) != pdTRUE)
    {
        set_status(out, "Reticulum directory queue busy", kAnnouncesPath);
        return out;
    }

    PendingAnnounce* slot = find_announce_slot(*state, record.destination_hash);
    const uint32_t previous_first_seen =
        slot && slot->occupied ? slot->record.first_seen_s : 0;
    if (slot)
    {
        *slot = PendingAnnounce{};
        slot->occupied = true;
        slot->order = state->next_order++;
        slot->record = record;
        if (previous_first_seen != 0 &&
            (slot->record.first_seen_s == 0 ||
             previous_first_seen < slot->record.first_seen_s))
        {
            slot->record.first_seen_s = previous_first_seen;
        }
        const std::size_t raw_len =
            record.raw_packet && record.raw_packet_len < kPendingRuntimeBlobBytes
                ? record.raw_packet_len
                : (record.raw_packet ? kPendingRuntimeBlobBytes : 0);
        const std::size_t app_len =
            record.app_data && record.app_data_len < kPendingRuntimeBlobBytes
                ? record.app_data_len
                : (record.app_data ? kPendingRuntimeBlobBytes : 0);
        if (raw_len != 0)
        {
            std::memcpy(slot->raw_packet, record.raw_packet, raw_len);
        }
        if (app_len != 0)
        {
            std::memcpy(slot->app_data, record.app_data, app_len);
        }
        slot->record.raw_packet_len = raw_len;
        slot->record.app_data_len = app_len;
        bind_pending_payloads(*slot);
    }
    xSemaphoreGive(state->mutex);

    if (!slot)
    {
        set_status(out, "Reticulum directory queue full", kAnnouncesPath);
        return out;
    }
    out.saved = true;
    set_status(out, "Reticulum announce queued", kAnnouncesPath);
    signal_async_worker(*state);
    return out;
}

Status queue_lxmf_address_async(const LxmfAddressRecord& record)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize) ||
        zero_hash(record.enc_pub, kReticulumPublicKeySize) ||
        zero_hash(record.sig_pub, kReticulumPublicKeySize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesPath);
        return out;
    }

    AsyncDirectoryState* state = ensure_async_worker();
    if (!state)
    {
        set_status(out, "Reticulum directory queue unavailable", kLxmfAddressesPath);
        return out;
    }
    if (xSemaphoreTake(state->mutex, kAsyncMutexWait) != pdTRUE)
    {
        set_status(out, "Reticulum directory queue busy", kLxmfAddressesPath);
        return out;
    }

    PendingAddress* slot = find_address_slot(*state, record.destination_hash);
    const uint32_t previous_first_seen =
        slot && slot->occupied ? slot->record.first_seen_s : 0;
    if (slot)
    {
        *slot = PendingAddress{};
        slot->occupied = true;
        slot->order = state->next_order++;
        slot->record = record;
        if (previous_first_seen != 0 &&
            (slot->record.first_seen_s == 0 ||
             previous_first_seen < slot->record.first_seen_s))
        {
            slot->record.first_seen_s = previous_first_seen;
        }
    }
    xSemaphoreGive(state->mutex);

    if (!slot)
    {
        set_status(out, "Reticulum directory queue full", kLxmfAddressesPath);
        return out;
    }
    out.saved = true;
    set_status(out, "LXMF address queued", kLxmfAddressesPath);
    signal_async_worker(*state);
    return out;
}

TsvFields split_tsv(std::string_view line)
{
    TsvFields out{};
    std::size_t start = 0;
    while (start <= line.size() && out.count < kMaxTsvFields)
    {
        const std::size_t end = line.find('\t', start);
        out.values[out.count++] =
            line.substr(start,
                        end == std::string::npos ? std::string::npos : end - start);
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return out;
}

bool data_line(std::string_view line)
{
    return !line.empty() && line[0] != '#' && line.rfind("version\t", 0) != 0;
}

bool first_field_matches(std::string_view line, std::string_view key)
{
    const std::size_t tab = line.find('\t');
    const std::string_view first =
        tab == std::string::npos ? line : line.substr(0, tab);
    return first == key;
}

uint32_t parse_u32(std::string_view text, uint32_t fallback = 0)
{
    if (text.empty())
    {
        return fallback;
    }
    uint32_t value = 0;
    for (char ch : text)
    {
        if (ch < '0' || ch > '9')
        {
            return fallback;
        }
        value = (value * 10U) + static_cast<uint32_t>(ch - '0');
    }
    return value;
}

bool truthy(std::string_view text)
{
    return text == "1" || text == "true" || text == "yes" || text == "enabled";
}

char lower_ascii(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
    {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

bool contains_ci(std::string_view text, std::string_view query)
{
    if (query.empty())
    {
        return true;
    }
    if (query.size() > text.size())
    {
        return false;
    }
    for (std::size_t start = 0; start + query.size() <= text.size(); ++start)
    {
        bool match = true;
        for (std::size_t offset = 0; offset < query.size(); ++offset)
        {
            if (lower_ascii(text[start + offset]) != lower_ascii(query[offset]))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}

bool address_line_matches_query(std::string_view line, std::string_view query)
{
    const std::string_view trimmed_query = trim_view(query);
    if (trimmed_query.empty())
    {
        return true;
    }
    const TsvFields fields = split_tsv(line);
    return (fields.count > 0 && contains_ci(fields.values[0], trimmed_query)) ||
           (fields.count > 1 && contains_ci(fields.values[1], trimmed_query)) ||
           (fields.count > 4 && contains_ci(fields.values[4], trimmed_query));
}

uint32_t node_id_from_destination_hash(
    const uint8_t destination_hash[kReticulumHashSize])
{
    if (!destination_hash)
    {
        return 0;
    }
    return (static_cast<uint32_t>(destination_hash[12]) << 24) |
           (static_cast<uint32_t>(destination_hash[13]) << 16) |
           (static_cast<uint32_t>(destination_hash[14]) << 8) |
           static_cast<uint32_t>(destination_hash[15]);
}

std::string bool_text(bool value)
{
    return value ? "1" : "0";
}

std::string source_text(EntrySource source)
{
    switch (source)
    {
    case EntrySource::RuntimeRx:
        return "runtime_rx";
    case EntrySource::PathResponse:
        return "path_response";
    case EntrySource::Manual:
        return "manual";
    case EntrySource::Import:
        return "import";
    case EntrySource::Unknown:
    default:
        return "unknown";
    }
}

EntrySource parse_source(std::string_view source)
{
    if (source == "runtime_rx")
    {
        return EntrySource::RuntimeRx;
    }
    if (source == "path_response")
    {
        return EntrySource::PathResponse;
    }
    if (source == "manual")
    {
        return EntrySource::Manual;
    }
    if (source == "import")
    {
        return EntrySource::Import;
    }
    return EntrySource::Unknown;
}

std::string aspect_text(AnnounceAspect aspect)
{
    switch (aspect)
    {
    case AnnounceAspect::LxmfDelivery:
        return "lxmf.delivery";
    case AnnounceAspect::LxmfPropagation:
        return "lxmf.propagation";
    case AnnounceAspect::CallAudio:
        return "call.audio";
    case AnnounceAspect::NomadNetworkNode:
        return "nomadnetwork.node";
    case AnnounceAspect::Unknown:
    default:
        return "unknown";
    }
}

AnnounceAspect parse_aspect(std::string_view aspect)
{
    if (aspect == "lxmf.delivery")
    {
        return AnnounceAspect::LxmfDelivery;
    }
    if (aspect == "lxmf.propagation")
    {
        return AnnounceAspect::LxmfPropagation;
    }
    if (aspect == "call.audio")
    {
        return AnnounceAspect::CallAudio;
    }
    if (aspect == "nomadnetwork.node")
    {
        return AnnounceAspect::NomadNetworkNode;
    }
    return AnnounceAspect::Unknown;
}

bool write_line(SdRuntimeFile& file, std::string_view line)
{
    return file.write(line.data(), line.size()) == line.size() &&
           file.write_byte('\n') == 1U;
}

bool write_text(SdRuntimeFile& file, const char* text)
{
    const std::size_t len = std::strlen(text);
    return file.write(text, len) == len;
}

template <typename Record>
void append_latest_record(Record* records,
                          std::size_t max_records,
                          std::size_t& count,
                          const Record& record)
{
    if (!records || max_records == 0)
    {
        return;
    }
    if (count < max_records)
    {
        records[count++] = record;
        return;
    }
    for (std::size_t index = 1; index < max_records; ++index)
    {
        records[index - 1U] = records[index];
    }
    records[max_records - 1U] = record;
}

template <typename Record>
void reverse_records(Record* records, std::size_t count)
{
    if (!records || count < 2)
    {
        return;
    }
    for (std::size_t left = 0, right = count - 1U; left < right; ++left, --right)
    {
        Record temp = records[left];
        records[left] = records[right];
        records[right] = temp;
    }
}

uint32_t find_existing_first_seen(const char* path,
                                  const std::string& destination,
                                  std::size_t first_seen_field,
                                  uint32_t fallback)
{
    if (!::platform::esp::arduino_common::storage::sd_exists(path))
    {
        return fallback;
    }
    SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return fallback;
    }
    std::string line;
    LineReader reader(file);
    while (reader.read_line(line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view) && first_field_matches(view, destination))
        {
            const TsvFields fields = split_tsv(view);
            file.close();
            return fields.count > first_seen_field
                       ? parse_u32(fields.values[first_seen_field], fallback)
                       : fallback;
        }
    }
    file.close();
    return fallback;
}

bool parse_address_line(std::string_view line, LxmfAddressRecord& out)
{
    const TsvFields fields = split_tsv(line);
    if (fields.count < 11)
    {
        return false;
    }
    LxmfAddressRecord parsed{};
    if (!parse_hex(fields.values[0], parsed.destination_hash, kReticulumHashSize) ||
        !parse_hex(fields.values[1], parsed.identity_hash, kReticulumHashSize) ||
        !parse_hex(fields.values[2], parsed.enc_pub, kReticulumPublicKeySize) ||
        !parse_hex(fields.values[3], parsed.sig_pub, kReticulumPublicKeySize))
    {
        return false;
    }
    copy_view(parsed.display_name, sizeof(parsed.display_name), fields.values[4]);
    parsed.favorite = truthy(fields.values[5]);
    parsed.ignored = truthy(fields.values[6]);
    parsed.trusted = truthy(fields.values[7]);
    parsed.source = parse_source(fields.values[8]);
    parsed.first_seen_s = parse_u32(fields.values[9]);
    parsed.last_seen_s = parse_u32(fields.values[10]);
    parsed.valid = true;
    out = parsed;
    return true;
}

bool parse_announce_line(std::string_view line, AnnounceRecord& out)
{
    const TsvFields fields = split_tsv(line);
    if (fields.count < 12)
    {
        return false;
    }
    AnnounceRecord parsed{};
    if (!parse_hex(fields.values[0], parsed.destination_hash, kReticulumHashSize) ||
        !parse_hex(fields.values[1], parsed.identity_hash, kReticulumHashSize))
    {
        return false;
    }
    parsed.aspect = parse_aspect(fields.values[2]);
    parsed.source = parse_source(fields.values[3]);
    parsed.first_seen_s = parse_u32(fields.values[4]);
    parsed.last_seen_s = parse_u32(fields.values[5]);
    parsed.hops = static_cast<uint8_t>(parse_u32(fields.values[6], 0xFF) & 0xFFU);
    parsed.path_response = truthy(fields.values[7]);
    parsed.local_destination = truthy(fields.values[8]);
    parsed.delivery = truthy(fields.values[9]);
    parsed.propagation = truthy(fields.values[10]);
    copy_view(parsed.display_name, sizeof(parsed.display_name), fields.values[11]);
    if (parsed.display_name[0] == '\0' && fields.count >= 14)
    {
        (void)copy_text_app_data_display_name_from_hex(fields.values[13],
                                                       parsed.display_name,
                                                       sizeof(parsed.display_name));
    }
    parsed.valid = true;
    out = parsed;
    return true;
}

void preserve_address_flags(const char* path,
                            const std::string& destination,
                            bool* favorite,
                            bool* ignored,
                            bool* trusted,
                            uint32_t* first_seen_s,
                            DirectoryIoGate io_gate)
{
    if (!io_gate_open(io_gate))
    {
        return;
    }
    if (!::platform::esp::arduino_common::storage::sd_exists(path))
    {
        return;
    }
    if (!io_gate_open(io_gate))
    {
        return;
    }
    SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return;
    }
    std::string line;
    LineReader reader(file);
    while (reader.read_line(line))
    {
        if (!io_gate_open(io_gate))
        {
            break;
        }
        const std::string_view view = trim_view(line);
        if (data_line(view) && first_field_matches(view, destination))
        {
            LxmfAddressRecord parsed{};
            if (parse_address_line(view, parsed))
            {
                if (favorite)
                {
                    *favorite = parsed.favorite;
                }
                if (ignored)
                {
                    *ignored = parsed.ignored;
                }
                if (trusted)
                {
                    *trusted = parsed.trusted;
                }
                if (first_seen_s && parsed.first_seen_s != 0)
                {
                    *first_seen_s = parsed.first_seen_s;
                }
            }
            break;
        }
    }
    file.close();
}

Status stream_upsert_line(const char* path,
                          const char* temp_path,
                          const char* title,
                          const char* success_message,
                          const std::string& destination,
                          const std::string& new_line,
                          DirectoryIoGate io_gate)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (!io_gate_open(io_gate))
    {
        set_maintenance_deferred(out, path);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", path);
        return out;
    }
    if (!ensure_config_dir())
    {
        set_status(out, "Cannot create Reticulum directory", kConfigDir);
        return out;
    }

    if (!io_gate_open(io_gate))
    {
        set_maintenance_deferred(out, path);
        return out;
    }
    out.file_present = ::platform::esp::arduino_common::storage::sd_exists(path);
    if (::platform::esp::arduino_common::storage::sd_exists(temp_path))
    {
        if (!io_gate_open(io_gate))
        {
            set_maintenance_deferred(out, temp_path);
            return out;
        }
        ::platform::esp::arduino_common::storage::sd_remove(temp_path);
    }

    if (!io_gate_open(io_gate))
    {
        set_maintenance_deferred(out, temp_path);
        return out;
    }
    SdRuntimeFile out_file;
    if (!out_file.open(temp_path, "w"))
    {
        set_status(out, "Cannot open Reticulum directory temp", temp_path);
        return out;
    }

    bool ok = write_text(out_file, "# Trail Mate Reticulum ") &&
              write_text(out_file, title) &&
              write_text(out_file, "\nversion\t1\n");

    std::size_t existing_data_lines = 0;
    if (ok && out.file_present)
    {
        if (!io_gate_open(io_gate))
        {
            out_file.close();
            set_maintenance_deferred(out, path);
            return out;
        }
        SdRuntimeFile count_file;
        if (!count_file.open(path, "r"))
        {
            out_file.close();
            ::platform::esp::arduino_common::storage::sd_remove(temp_path);
            set_status(out, "Cannot read Reticulum directory", path);
            return out;
        }
        std::string old_line;
        LineReader reader(count_file);
        while (reader.read_line(old_line))
        {
            if (!io_gate_open(io_gate))
            {
                count_file.close();
                out_file.close();
                set_maintenance_deferred(out, path);
                return out;
            }
            const std::string_view view = trim_view(old_line);
            if (data_line(view) && !first_field_matches(view, destination))
            {
                ++existing_data_lines;
            }
        }
        count_file.close();
    }

    const std::size_t skip_oldest =
        existing_data_lines + 1U > kMaxDirectoryEntries
            ? (existing_data_lines + 1U - kMaxDirectoryEntries)
            : 0;
    std::size_t kept = 0;
    std::size_t skipped = 0;
    if (ok && out.file_present)
    {
        if (!io_gate_open(io_gate))
        {
            out_file.close();
            set_maintenance_deferred(out, path);
            return out;
        }
        SdRuntimeFile in_file;
        if (!in_file.open(path, "r"))
        {
            out_file.close();
            ::platform::esp::arduino_common::storage::sd_remove(temp_path);
            set_status(out, "Cannot read Reticulum directory", path);
            return out;
        }
        std::string old_line;
        LineReader reader(in_file);
        while (reader.read_line(old_line))
        {
            if (!io_gate_open(io_gate))
            {
                in_file.close();
                out_file.close();
                set_maintenance_deferred(out, path);
                return out;
            }
            const std::string_view view = trim_view(old_line);
            if (data_line(view) && !first_field_matches(view, destination))
            {
                if (skipped < skip_oldest)
                {
                    ++skipped;
                    continue;
                }
                ok = write_line(out_file, view);
                if (!ok)
                {
                    break;
                }
                ++kept;
            }
        }
        in_file.close();
    }

    if (!io_gate_open(io_gate))
    {
        out_file.close();
        set_maintenance_deferred(out, temp_path);
        return out;
    }
    ok = ok && write_line(out_file, new_line);
    out_file.close();
    if (!ok)
    {
        ::platform::esp::arduino_common::storage::sd_remove(temp_path);
        set_status(out, "Cannot write Reticulum directory", temp_path);
        return out;
    }

    if (!io_gate_open(io_gate))
    {
        set_maintenance_deferred(out, temp_path);
        return out;
    }
    if (::platform::esp::arduino_common::storage::sd_exists(path))
    {
        if (!io_gate_open(io_gate))
        {
            set_maintenance_deferred(out, path);
            return out;
        }
        ::platform::esp::arduino_common::storage::sd_remove(path);
    }
    if (!::platform::esp::arduino_common::storage::sd_rename(temp_path, path))
    {
        ::platform::esp::arduino_common::storage::sd_remove(temp_path);
        set_status(out, "Cannot replace Reticulum directory", path);
        return out;
    }

    out.file_present = true;
    out.saved = true;
    set_status(out, success_message, path);
    return out;
}

std::string announce_line(const AnnounceRecord& record, uint32_t first_seen_s)
{
    std::string line;
    line.reserve(256U + (record.raw_packet_len * 2U) + (record.app_data_len * 2U));
    line += hex_text(record.destination_hash, kReticulumHashSize);
    line += "\t";
    line += hex_text(record.identity_hash, kReticulumHashSize);
    line += "\t";
    line += aspect_text(record.aspect);
    line += "\t";
    line += source_text(record.source);
    line += "\t";
    line += std::to_string(first_seen_s);
    line += "\t";
    line += std::to_string(record.last_seen_s);
    line += "\t";
    line += std::to_string(static_cast<unsigned>(record.hops));
    line += "\t";
    line += bool_text(record.path_response);
    line += "\t";
    line += bool_text(record.local_destination);
    line += "\t";
    line += bool_text(record.delivery);
    line += "\t";
    line += bool_text(record.propagation);
    line += "\t";
    line += sanitize_field(record.display_name);
    line += "\t";
    append_hex(line, record.raw_packet, record.raw_packet_len);
    line += "\t";
    append_hex(line, record.app_data, record.app_data_len);
    return line;
}

std::string address_line(const LxmfAddressRecord& record,
                         bool favorite,
                         bool ignored,
                         bool trusted,
                         uint32_t first_seen_s)
{
    std::string line;
    line.reserve(256);
    line += hex_text(record.destination_hash, kReticulumHashSize);
    line += "\t";
    line += hex_text(record.identity_hash, kReticulumHashSize);
    line += "\t";
    line += hex_text(record.enc_pub, kReticulumPublicKeySize);
    line += "\t";
    line += hex_text(record.sig_pub, kReticulumPublicKeySize);
    line += "\t";
    line += sanitize_field(record.display_name);
    line += "\t";
    line += bool_text(favorite);
    line += "\t";
    line += bool_text(ignored);
    line += "\t";
    line += bool_text(trusted);
    line += "\t";
    line += source_text(record.source);
    line += "\t";
    line += std::to_string(first_seen_s);
    line += "\t";
    line += std::to_string(record.last_seen_s);
    return line;
}

Status record_announce_sync(const AnnounceRecord& record, bool require_maintenance)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid Reticulum announce", kAnnouncesPath);
        return out;
    }
    DirectoryIoGate io_gate = require_maintenance ? maintenance_window : nullptr;
    if (!io_gate_open(io_gate))
    {
        set_maintenance_deferred(out, kAnnouncesPath);
        return out;
    }

    const std::string destination = hex_text(record.destination_hash, kReticulumHashSize);
    const uint32_t fallback_first_seen =
        record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    const uint32_t first_seen_s = out.sd_present && !require_maintenance
                                      ? find_existing_first_seen(kAnnouncesPath,
                                                                 destination,
                                                                 4,
                                                                 fallback_first_seen)
                                      : fallback_first_seen;
    return stream_upsert_line(kAnnouncesPath,
                              kAnnouncesTempPath,
                              "announces",
                              "Reticulum announce saved",
                              destination,
                              announce_line(record, first_seen_s),
                              io_gate);
}

Status record_lxmf_address_sync(const LxmfAddressRecord& record, bool require_maintenance)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize) ||
        zero_hash(record.enc_pub, kReticulumPublicKeySize) ||
        zero_hash(record.sig_pub, kReticulumPublicKeySize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }
    DirectoryIoGate io_gate = require_maintenance ? maintenance_window : nullptr;
    if (!io_gate_open(io_gate))
    {
        set_maintenance_deferred(out, kLxmfAddressesPath);
        return out;
    }

    const std::string destination = hex_text(record.destination_hash, kReticulumHashSize);
    const bool requested_favorite = record.favorite;
    const bool requested_ignored = record.ignored;
    const bool requested_trusted = record.trusted;
    bool favorite = record.favorite;
    bool ignored = record.ignored;
    bool trusted = record.trusted;
    uint32_t first_seen_s = record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    if (out.sd_present)
    {
        preserve_address_flags(kLxmfAddressesPath,
                               destination,
                               &favorite,
                               &ignored,
                               &trusted,
                               &first_seen_s,
                               io_gate);
    }
    favorite = favorite || requested_favorite;
    ignored = ignored || requested_ignored;
    trusted = trusted || requested_trusted;
    if (!io_gate_open(io_gate))
    {
        set_maintenance_deferred(out, kLxmfAddressesPath);
        return out;
    }

    return stream_upsert_line(kLxmfAddressesPath,
                              kLxmfAddressesTempPath,
                              "LXMF addresses",
                              "LXMF address saved",
                              destination,
                              address_line(record, favorite, ignored, trusted, first_seen_s),
                              io_gate);
}

} // namespace

const char* announces_path()
{
    return kAnnouncesPath;
}

const char* lxmf_addresses_path()
{
    return kLxmfAddressesPath;
}

Status record_announce(const AnnounceRecord& record)
{
    return queue_announce_async(record);
}

Status record_lxmf_address(const LxmfAddressRecord& record)
{
    return queue_lxmf_address_async(record);
}

Status record_lxmf_address_now(const LxmfAddressRecord& record)
{
    return record_lxmf_address_sync(record, false);
}

Status set_lxmf_address_favorite_now(
    const uint8_t destination_hash[kReticulumHashSize],
    bool favorite)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (!destination_hash || zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesPath);
        return out;
    }

    LxmfAddressRecord record{};
    Status find_status = find_lxmf_address_by_destination(destination_hash, &record);
    out.file_present = find_status.file_present;
    if (!find_status.loaded || !record.valid)
    {
        set_status(out, "LXMF address not found", kLxmfAddressesPath);
        return out;
    }

    record.favorite = favorite;
    const std::string destination = hex_text(record.destination_hash, kReticulumHashSize);
    return stream_upsert_line(kLxmfAddressesPath,
                              kLxmfAddressesTempPath,
                              "LXMF addresses",
                              "LXMF address saved",
                              destination,
                              address_line(record,
                                           record.favorite,
                                           record.ignored,
                                           record.trusted,
                                           record.first_seen_s),
                              nullptr);
}

Status load_announces(AnnounceRecord* out_records,
                      std::size_t max_records,
                      std::size_t* out_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (out_count)
    {
        *out_count = 0;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "Reticulum announce storage unavailable", kAnnouncesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kAnnouncesPath);
        return out;
    }

    out.file_present = ::platform::esp::arduino_common::storage::sd_exists(kAnnouncesPath);
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No Reticulum announces", kAnnouncesPath);
        return out;
    }

    SdRuntimeFile file;
    if (!file.open(kAnnouncesPath, "r"))
    {
        set_status(out, "Cannot read Reticulum announces", kAnnouncesPath);
        return out;
    }

    std::size_t count = 0;
    std::string line;
    LineReader reader(file);
    while (reader.read_line(line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view))
        {
            AnnounceRecord parsed{};
            if (parse_announce_line(view, parsed))
            {
                append_latest_record(out_records, max_records, count, parsed);
            }
        }
    }
    file.close();

    reverse_records(out_records, count);
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out, count == 0 ? "No Reticulum announces" : "Reticulum announces loaded",
               kAnnouncesPath);
    return out;
}

Status load_lxmf_addresses(LxmfAddressRecord* out_records,
                           std::size_t max_records,
                           std::size_t* out_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (out_count)
    {
        *out_count = 0;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "LXMF address storage unavailable", kLxmfAddressesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesPath);
        return out;
    }

    out.file_present = ::platform::esp::arduino_common::storage::sd_exists(kLxmfAddressesPath);
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No LXMF addresses", kLxmfAddressesPath);
        return out;
    }

    SdRuntimeFile file;
    if (!file.open(kLxmfAddressesPath, "r"))
    {
        set_status(out, "Cannot read LXMF addresses", kLxmfAddressesPath);
        return out;
    }

    std::size_t count = 0;
    std::string line;
    LineReader reader(file);
    while (reader.read_line(line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view))
        {
            LxmfAddressRecord parsed{};
            if (parse_address_line(view, parsed))
            {
                append_latest_record(out_records, max_records, count, parsed);
            }
        }
    }
    file.close();

    reverse_records(out_records, count);
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out, count == 0 ? "No LXMF addresses" : "LXMF addresses loaded",
               kLxmfAddressesPath);
    return out;
}

Status load_lxmf_addresses_matching(const char* query,
                                    LxmfAddressRecord* out_records,
                                    std::size_t max_records,
                                    std::size_t* out_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (out_count)
    {
        *out_count = 0;
    }
    if (!query || trim_view(query).empty())
    {
        return load_lxmf_addresses(out_records, max_records, out_count);
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "LXMF address storage unavailable", kLxmfAddressesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesPath);
        return out;
    }

    out.file_present = ::platform::esp::arduino_common::storage::sd_exists(kLxmfAddressesPath);
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No LXMF addresses", kLxmfAddressesPath);
        return out;
    }

    SdRuntimeFile file;
    if (!file.open(kLxmfAddressesPath, "r"))
    {
        set_status(out, "Cannot read LXMF addresses", kLxmfAddressesPath);
        return out;
    }

    std::size_t count = 0;
    std::string line;
    LineReader reader(file);
    while (reader.read_line(line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view) && address_line_matches_query(view, query))
        {
            LxmfAddressRecord parsed{};
            if (parse_address_line(view, parsed))
            {
                append_latest_record(out_records, max_records, count, parsed);
            }
        }
    }
    file.close();

    reverse_records(out_records, count);
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out, count == 0 ? "No LXMF address matches" : "LXMF address matches loaded",
               kLxmfAddressesPath);
    return out;
}

Status find_lxmf_address_by_destination(
    const uint8_t destination_hash[kReticulumHashSize],
    LxmfAddressRecord* out_record)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    if (!destination_hash || !out_record ||
        zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesPath);
        return out;
    }

    out.file_present = ::platform::esp::arduino_common::storage::sd_exists(kLxmfAddressesPath);
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No LXMF addresses", kLxmfAddressesPath);
        return out;
    }

    SdRuntimeFile file;
    if (!file.open(kLxmfAddressesPath, "r"))
    {
        set_status(out, "Cannot read LXMF addresses", kLxmfAddressesPath);
        return out;
    }

    const std::string destination = hex_text(destination_hash, kReticulumHashSize);
    std::string line;
    LineReader reader(file);
    while (reader.read_line(line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view) && first_field_matches(view, destination))
        {
            out.loaded = parse_address_line(view, *out_record);
            break;
        }
    }
    file.close();
    set_status(out,
               out.loaded ? "LXMF address loaded" : "LXMF address not found",
               kLxmfAddressesPath);
    return out;
}

Status find_lxmf_address_by_node_id(uint32_t node_id,
                                    LxmfAddressRecord* out_record)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    if (node_id == 0 || !out_record)
    {
        set_status(out, "Invalid LXMF node", kLxmfAddressesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesPath);
        return out;
    }

    out.file_present = ::platform::esp::arduino_common::storage::sd_exists(kLxmfAddressesPath);
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No LXMF addresses", kLxmfAddressesPath);
        return out;
    }

    SdRuntimeFile file;
    if (!file.open(kLxmfAddressesPath, "r"))
    {
        set_status(out, "Cannot read LXMF addresses", kLxmfAddressesPath);
        return out;
    }

    std::string line;
    LineReader reader(file);
    bool found = false;
    while (reader.read_line(line))
    {
        const std::string_view view = trim_view(line);
        if (!data_line(view))
        {
            continue;
        }
        LxmfAddressRecord parsed{};
        if (parse_address_line(view, parsed) &&
            node_id_from_destination_hash(parsed.destination_hash) == node_id)
        {
            *out_record = parsed;
            found = true;
        }
    }
    file.close();
    out.loaded = found;
    set_status(out,
               found ? "LXMF address loaded" : "LXMF address not found",
               kLxmfAddressesPath);
    return out;
}

} // namespace platform::ui::reticulum_directory
