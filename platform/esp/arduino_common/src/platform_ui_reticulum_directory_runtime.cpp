#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"

#include "chat/ports/i_mesh_peer_directory.h"
#include "platform/esp/arduino_common/storage/persistence_bus_gate.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_bus_arbiter.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_runtime.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cctype>
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
constexpr const char* kLxmfAddressesPath = "/mesh/peers.bin";
constexpr std::size_t kMaxDirectoryEntries = 1024;
constexpr std::size_t kMaxLineBytes = 4096;
constexpr std::size_t kMaxTsvFields = 14;
constexpr std::size_t kLineReadChunkBytes = 256;
constexpr std::size_t kPendingAnnounceDepth = 16;
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

struct AsyncDirectoryState
{
    SemaphoreHandle_t mutex = nullptr;
    QueueHandle_t signal = nullptr;
    TaskHandle_t task = nullptr;
    uint32_t next_order = 1;
    PendingAnnounce announces[kPendingAnnounceDepth]{};
    PendingAnnounce active_announce{};
};

AsyncDirectoryState* s_async_state = nullptr;
chat::IMeshPeerDirectory* s_mesh_peer_directory = nullptr;
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

chat::MeshPeerSource mesh_source_from_entry_source(EntrySource source)
{
    switch (source)
    {
    case EntrySource::RuntimeRx:
        return chat::MeshPeerSource::RuntimeRx;
    case EntrySource::PathResponse:
        return chat::MeshPeerSource::DiscoveryResponse;
    case EntrySource::Manual:
        return chat::MeshPeerSource::Manual;
    case EntrySource::Import:
        return chat::MeshPeerSource::Import;
    case EntrySource::Unknown:
    default:
        return chat::MeshPeerSource::Unknown;
    }
}

EntrySource entry_source_from_mesh_source(chat::MeshPeerSource source)
{
    switch (source)
    {
    case chat::MeshPeerSource::RuntimeRx:
        return EntrySource::RuntimeRx;
    case chat::MeshPeerSource::DiscoveryResponse:
        return EntrySource::PathResponse;
    case chat::MeshPeerSource::Manual:
        return EntrySource::Manual;
    case chat::MeshPeerSource::Import:
        return EntrySource::Import;
    case chat::MeshPeerSource::Unknown:
    default:
        return EntrySource::Unknown;
    }
}

void init_lxmf_directory_status(Status& out)
{
    out.supported = true;
    out.sd_present = sd_available();
    out.file_present = s_mesh_peer_directory != nullptr;
}

chat::IMeshPeerDirectory* require_mesh_peer_directory(Status& out)
{
    init_lxmf_directory_status(out);
    if (!s_mesh_peer_directory)
    {
        set_status(out, "Mesh peer directory unavailable", kLxmfAddressesPath);
        return nullptr;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesPath);
        return nullptr;
    }
    return s_mesh_peer_directory;
}

void set_mesh_peer_directory_failure(Status& out,
                                     chat::MeshPeerDirectoryStatusCode code,
                                     const char* not_found_message,
                                     const char* invalid_message)
{
    switch (code)
    {
    case chat::MeshPeerDirectoryStatusCode::NotFound:
        set_status(out, not_found_message, kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::InvalidArgument:
        set_status(out, invalid_message, kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::StorageUnavailable:
        set_status(out, "Mesh peer directory storage unavailable", kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::IoError:
        set_status(out, "Cannot access mesh peer directory", kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::CapacityExceeded:
        set_status(out, "Mesh peer directory full", kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::Unsupported:
        set_status(out, "Mesh peer directory unsupported", kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::Ok:
    default:
        set_status(out, "Mesh peer directory failed", kLxmfAddressesPath);
        break;
    }
}

bool lxmf_address_to_mesh_peer_record(const LxmfAddressRecord& record,
                                      chat::MeshPeerRecord& out_record)
{
    out_record = chat::MeshPeerRecord{};
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize) ||
        zero_hash(record.enc_pub, kReticulumPublicKeySize) ||
        zero_hash(record.sig_pub, kReticulumPublicKeySize))
    {
        return false;
    }

    const chat::ReticulumPeerIdentity identity =
        chat::makeReticulumPeerIdentity(record.destination_hash,
                                        record.identity_hash);
    out_record.valid = true;
    out_record.identity = chat::makeMeshPeerReticulumIdentity(identity);
    out_record.source = mesh_source_from_entry_source(record.source);
    out_record.first_seen_s =
        record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    out_record.last_seen_s =
        record.last_seen_s != 0 ? record.last_seen_s : out_record.first_seen_s;
    chat::copyMeshPeerText(out_record.display_name,
                           sizeof(out_record.display_name),
                           record.display_name);
    out_record.flags.favorite = record.favorite;
    out_record.flags.ignored = record.ignored;
    out_record.flags.trusted = record.trusted;
    out_record.reticulum.identity = identity;
    out_record.reticulum.has_public_keys = true;
    std::memcpy(out_record.reticulum.enc_pub,
                record.enc_pub,
                sizeof(out_record.reticulum.enc_pub));
    std::memcpy(out_record.reticulum.sig_pub,
                record.sig_pub,
                sizeof(out_record.reticulum.sig_pub));
    out_record.reticulum.delivery = true;
    return chat::meshPeerRecordIsValid(out_record);
}

bool mesh_peer_record_to_lxmf_address(const chat::MeshPeerRecord& record,
                                      LxmfAddressRecord& out_record)
{
    out_record = LxmfAddressRecord{};
    if (!chat::meshPeerRecordIsValid(record) ||
        !chat::meshPeerIsReticulumProtocol(record.identity.protocol))
    {
        return false;
    }

    chat::ReticulumPeerIdentity identity = record.reticulum.identity.valid
                                               ? record.reticulum.identity
                                               : record.identity.reticulum;
    if (!identity.valid ||
        zero_hash(identity.destination_hash, kReticulumHashSize))
    {
        return false;
    }

    out_record.valid = true;
    std::memcpy(out_record.destination_hash,
                identity.destination_hash,
                sizeof(out_record.destination_hash));
    std::memcpy(out_record.identity_hash,
                identity.identity_hash,
                sizeof(out_record.identity_hash));
    if (record.reticulum.has_public_keys)
    {
        std::memcpy(out_record.enc_pub,
                    record.reticulum.enc_pub,
                    sizeof(out_record.enc_pub));
        std::memcpy(out_record.sig_pub,
                    record.reticulum.sig_pub,
                    sizeof(out_record.sig_pub));
    }
    copy_text(out_record.display_name,
              sizeof(out_record.display_name),
              record.display_name);
    out_record.favorite = record.flags.favorite;
    out_record.ignored = record.flags.ignored;
    out_record.trusted = record.flags.trusted;
    out_record.source = entry_source_from_mesh_source(record.source);
    out_record.first_seen_s = record.first_seen_s;
    out_record.last_seen_s = record.last_seen_s;
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

bool has_pending_locked(const AsyncDirectoryState& state)
{
    for (const auto& pending : state.announces)
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

bool pop_oldest_pending(AsyncDirectoryState& state)
{
    if (xSemaphoreTake(state.mutex, portMAX_DELAY) != pdTRUE)
    {
        return false;
    }

    PendingAnnounce* oldest_announce = nullptr;
    for (auto& pending : state.announces)
    {
        if (pending.occupied &&
            (!oldest_announce || pending.order < oldest_announce->order))
        {
            oldest_announce = &pending;
        }
    }

    if (oldest_announce)
    {
        state.active_announce = *oldest_announce;
        bind_pending_payloads(state.active_announce);
        *oldest_announce = PendingAnnounce{};
    }

    const bool popped = oldest_announce != nullptr;
    xSemaphoreGive(state.mutex);
    return popped;
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

        if (!pop_oldest_pending(*state))
        {
            continue;
        }
        if (!maintenance_window())
        {
            log_maintenance_deferred("pre_flush");
            requeue_active_announce(*state);
            vTaskDelay(kAsyncMaintenanceClosedDelay);
            continue;
        }

        Status status{};
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
                                  uint32_t fallback,
                                  DirectoryIoGate io_gate,
                                  bool* out_deferred)
{
    if (out_deferred)
    {
        *out_deferred = false;
    }
    if (!io_gate_open(io_gate))
    {
        if (out_deferred)
        {
            *out_deferred = true;
        }
        return fallback;
    }
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
    for (;;)
    {
        if (!io_gate_open(io_gate))
        {
            file.close();
            if (out_deferred)
            {
                *out_deferred = true;
            }
            return fallback;
        }
        if (!reader.read_line(line))
        {
            break;
        }
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
    bool first_seen_scan_deferred = false;
    const uint32_t first_seen_s =
        out.sd_present
            ? find_existing_first_seen(kAnnouncesPath,
                                       destination,
                                       4,
                                       fallback_first_seen,
                                       io_gate,
                                       &first_seen_scan_deferred)
            : fallback_first_seen;
    if (first_seen_scan_deferred)
    {
        set_maintenance_deferred(out, kAnnouncesPath);
        return out;
    }
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
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }

    chat::MeshPeerRecord mesh_record{};
    if (!lxmf_address_to_mesh_peer_record(record, mesh_record))
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

    chat::MeshPeerUserFlags merged_flags = mesh_record.flags;
    chat::MeshPeerRecord existing{};
    if (directory->find(mesh_record.identity, existing).succeeded())
    {
        merged_flags.favorite = existing.flags.favorite || record.favorite;
        merged_flags.ignored = existing.flags.ignored || record.ignored;
        merged_flags.trusted = existing.flags.trusted || record.trusted;
    }

    const chat::MeshPeerDirectoryStatus status = directory->record(mesh_record);
    if (!status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF address");
        return out;
    }

    if (record.favorite || record.ignored || record.trusted)
    {
        const chat::MeshPeerDirectoryStatus flag_status =
            directory->setUserFlags(mesh_record.identity, merged_flags);
        if (!flag_status.succeeded())
        {
            set_mesh_peer_directory_failure(out,
                                            flag_status.code,
                                            "LXMF address not found",
                                            "Invalid LXMF address");
            return out;
        }
    }

    out.saved = true;
    out.loaded = true;
    set_status(out, "LXMF address saved", kLxmfAddressesPath);
    return out;
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

void bind_mesh_peer_directory(chat::IMeshPeerDirectory* directory)
{
    s_mesh_peer_directory = directory;
}

Status record_announce(const AnnounceRecord& record)
{
    return queue_announce_async(record);
}

Status record_lxmf_address(const LxmfAddressRecord& record)
{
    return record_lxmf_address_sync(record, false);
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
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!destination_hash || zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }

    const chat::ReticulumPeerIdentity reticulum_identity =
        chat::makeReticulumDestinationIdentity(destination_hash);
    const chat::MeshPeerIdentity identity =
        chat::makeMeshPeerReticulumIdentity(reticulum_identity);
    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus find_status =
        directory->find(identity, record);
    if (!find_status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        find_status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF address");
        return out;
    }

    chat::MeshPeerUserFlags flags = record.flags;
    flags.favorite = favorite;
    const chat::MeshPeerDirectoryStatus flag_status =
        directory->setUserFlags(record.identity, flags);
    if (!flag_status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        flag_status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF address");
        return out;
    }

    out.saved = true;
    out.loaded = true;
    set_status(out, "LXMF address saved", kLxmfAddressesPath);
    return out;
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
    if (out_count)
    {
        *out_count = 0;
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "LXMF address storage unavailable", kLxmfAddressesPath);
        return out;
    }

    chat::MeshPeerRecord* records =
        new (std::nothrow) chat::MeshPeerRecord[max_records];
    if (!records)
    {
        set_status(out, "LXMF address buffer unavailable", kLxmfAddressesPath);
        return out;
    }

    std::size_t loaded_count = 0;
    const chat::MeshPeerDirectoryStatus status =
        directory->loadRecent(chat::MeshProtocol::Reticulum,
                              records,
                              max_records,
                              &loaded_count);
    if (!status.succeeded())
    {
        delete[] records;
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "No LXMF addresses",
                                        "LXMF address storage unavailable");
        return out;
    }

    std::size_t count = 0;
    for (std::size_t index = 0; index < loaded_count && count < max_records; ++index)
    {
        LxmfAddressRecord converted{};
        if (mesh_peer_record_to_lxmf_address(records[index], converted))
        {
            out_records[count++] = converted;
        }
    }
    delete[] records;
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
    if (out_count)
    {
        *out_count = 0;
    }
    if (!query || trim_view(query).empty())
    {
        return load_lxmf_addresses(out_records, max_records, out_count);
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "LXMF address storage unavailable", kLxmfAddressesPath);
        return out;
    }

    chat::MeshPeerRecord* records =
        new (std::nothrow) chat::MeshPeerRecord[max_records];
    if (!records)
    {
        set_status(out, "LXMF address buffer unavailable", kLxmfAddressesPath);
        return out;
    }

    std::size_t loaded_count = 0;
    const chat::MeshPeerDirectoryStatus status =
        directory->search(chat::MeshProtocol::Reticulum,
                          query,
                          records,
                          max_records,
                          &loaded_count);
    if (!status.succeeded())
    {
        delete[] records;
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "No LXMF address matches",
                                        "LXMF address storage unavailable");
        return out;
    }

    std::size_t count = 0;
    for (std::size_t index = 0; index < loaded_count && count < max_records; ++index)
    {
        LxmfAddressRecord converted{};
        if (mesh_peer_record_to_lxmf_address(records[index], converted))
        {
            out_records[count++] = converted;
        }
    }
    delete[] records;
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
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!destination_hash || !out_record ||
        zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }

    const chat::ReticulumPeerIdentity reticulum_identity =
        chat::makeReticulumDestinationIdentity(destination_hash);
    const chat::MeshPeerIdentity identity =
        chat::makeMeshPeerReticulumIdentity(reticulum_identity);
    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus status = directory->find(identity, record);
    if (!status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF address");
        return out;
    }
    if (!mesh_peer_record_to_lxmf_address(record, *out_record))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }

    out.loaded = true;
    set_status(out, "LXMF address loaded", kLxmfAddressesPath);
    return out;
}

Status find_lxmf_address_by_node_id(uint32_t node_id,
                                    LxmfAddressRecord* out_record)
{
    Status out{};
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (node_id == 0 || !out_record)
    {
        set_status(out, "Invalid LXMF node", kLxmfAddressesPath);
        return out;
    }

    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus status =
        directory->findByNodeId(chat::MeshProtocol::Reticulum, node_id, record);
    if (!status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF node");
        return out;
    }
    if (!mesh_peer_record_to_lxmf_address(record, *out_record))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }

    out.loaded = true;
    set_status(out, "LXMF address loaded", kLxmfAddressesPath);
    return out;
}

} // namespace platform::ui::reticulum_directory

namespace platform::ui::reticulum_page
{
namespace
{

using ::platform::esp::arduino_common::storage::SdRuntimeFile;

constexpr const char* kPagesDir = "/trailmate/reticulum/pages";
constexpr const char* kDefaultPagePath = "/page/index.mu";
constexpr uint32_t kPageCacheReadWaitMs = 60;
constexpr uint32_t kPageCacheWriteWaitMs = 120;
constexpr uint32_t kPageCacheBusResource = 5;
constexpr uint32_t kPageCacheBusOwnerId = 0x4E504147u; // 'NPAG'
constexpr const char* kPageCacheBusOwner = "reticulum_page_cache";

::platform::esp::common::SharedSpiBusAdapter s_page_cache_bus_adapter(
    kPageCacheBusOwner,
    kPageCacheBusOwnerId);
::platform::esp::common::FixedSharedSpiBusPolicyStrategy s_page_cache_bus_policy(
    kPageCacheReadWaitMs,
    kPageCacheReadWaitMs,
    kPageCacheWriteWaitMs,
    kPageCacheWriteWaitMs);
sys::runtime::StorageBusArbiter s_page_cache_bus_arbiter(
    s_page_cache_bus_adapter,
    s_page_cache_bus_policy);

RequestStartHandler s_request_handler = nullptr;
void* s_request_context = nullptr;
constexpr std::size_t kPageProgressDepth = 4;
constexpr TickType_t kPageCacheAsyncMutexWait = pdMS_TO_TICKS(5);
constexpr uint32_t kPageCacheAsyncTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kPageCacheAsyncTaskPriority = tskIDLE_PRIORITY + 1;

struct PageCacheLoadState
{
    SemaphoreHandle_t mutex = nullptr;
    QueueHandle_t signal = nullptr;
    TaskHandle_t task = nullptr;
    bool pending = false;
    bool active = false;
    bool completed = false;
    char pending_destination[kReticulumPageDestinationTextSize] = {};
    char pending_path[kReticulumPagePathSize] = {};
    char active_destination[kReticulumPageDestinationTextSize] = {};
    char active_path[kReticulumPagePathSize] = {};
    char completed_destination[kReticulumPageDestinationTextSize] = {};
    char completed_path[kReticulumPagePathSize] = {};
    Status active_status{};
    Status completed_status{};
    std::size_t completed_body_len = 0;
    char completed_body[kReticulumPageBodyMaxBytes + 1U] = {};
    uint32_t invalidation_epoch = 0;
    uint32_t active_invalidation_epoch = 0;
};

struct PageRequestProgressSlot
{
    bool occupied = false;
    uint32_t order = 0;
    char destination[kReticulumPageDestinationTextSize] = {};
    char path[kReticulumPagePathSize] = {};
    RequestProgress progress{};
};

PageCacheLoadState* s_page_cache_load_state = nullptr;
PageRequestProgressSlot s_request_progress[kPageProgressDepth]{};
uint32_t s_request_progress_order = 1;
portMUX_TYPE s_request_progress_lock = portMUX_INITIALIZER_UNLOCKED;

enum class PageCacheBusAccess : uint8_t
{
    Read = 1,
    Write,
};

class PageCacheBusGate final
{
  public:
    explicit PageCacheBusGate(PageCacheBusAccess access)
        : gate_(s_page_cache_bus_arbiter,
                policyFor(access),
                waitMsFor(access),
                kPageCacheBusResource,
                kPageCacheBusOwnerId + static_cast<uint32_t>(access),
                kPageCacheBusOwnerId)
    {
    }

    bool locked() const
    {
        return gate_.locked();
    }

  private:
    static sys::runtime::BusAccessPolicy policyFor(PageCacheBusAccess access)
    {
        return access == PageCacheBusAccess::Write
                   ? sys::runtime::BusAccessPolicy::DurableCommit
                   : sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
    }

    static uint32_t waitMsFor(PageCacheBusAccess access)
    {
        return access == PageCacheBusAccess::Write ? kPageCacheWriteWaitMs
                                                   : kPageCacheReadWaitMs;
    }

    ::platform::esp::arduino_common::storage::PersistenceBusGate gate_;
};

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message, const char* detail = nullptr)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

bool page_sd_available()
{
    return ::platform::ui::device::card_ready() &&
           ::platform::esp::arduino_common::storage::sd_card_ready();
}

void set_busy_status(Status& out,
                     const char* message,
                     const char* detail = nullptr,
                     int progress_percent = 0)
{
    out.supported = true;
    out.sd_present = page_sd_available();
    out.busy = true;
    out.progress_percent = progress_percent;
    set_status(out, message, detail);
}

bool same_page_request(const char* lhs_destination,
                       const char* lhs_path,
                       const char* rhs_destination,
                       const char* rhs_path)
{
    return lhs_destination && lhs_path && rhs_destination && rhs_path &&
           std::strcmp(lhs_destination, rhs_destination) == 0 &&
           std::strcmp(lhs_path, rhs_path) == 0;
}

bool is_hex_char(char ch)
{
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

char uppercase_hex(char ch)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

bool normalize_destination(const char* destination_hash,
                           char* out_hash,
                           std::size_t out_len)
{
    if (!destination_hash || !out_hash ||
        out_len < kReticulumPageDestinationTextSize)
    {
        return false;
    }
    for (std::size_t i = 0; i < kReticulumPageDestinationTextSize - 1U; ++i)
    {
        if (!is_hex_char(destination_hash[i]))
        {
            return false;
        }
        out_hash[i] = uppercase_hex(destination_hash[i]);
    }
    out_hash[kReticulumPageDestinationTextSize - 1U] = '\0';
    return destination_hash[kReticulumPageDestinationTextSize - 1U] == '\0';
}

bool destination_to_bytes(
    const char* destination_hash,
    uint8_t out_hash[kReticulumPageDestinationTextSize / 2U])
{
    if (!destination_hash || !out_hash)
    {
        return false;
    }
    for (std::size_t i = 0; i < kReticulumPageDestinationTextSize / 2U; ++i)
    {
        const int hi = hex_value(destination_hash[i * 2U]);
        const int lo = hex_value(destination_hash[i * 2U + 1U]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        out_hash[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool allowed_path_char(char ch)
{
    const auto value = static_cast<unsigned char>(ch);
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || ch == '/' || ch == '-' ||
           ch == '_' || ch == '.';
}

bool path_has_parent_segment(const char* path)
{
    if (!path)
    {
        return true;
    }
    const char* segment = path;
    while (*segment != '\0')
    {
        while (*segment == '/')
        {
            ++segment;
        }
        const char* end = segment;
        while (*end != '\0' && *end != '/')
        {
            ++end;
        }
        if ((end - segment) == 2 && segment[0] == '.' && segment[1] == '.')
        {
            return true;
        }
        segment = end;
    }
    return false;
}

std::string cache_path(const char* destination_hash, const char* path)
{
    std::string out = kPagesDir;
    out += "/";
    out += destination_hash;
    out += path;
    return out;
}

bool ensure_dir(const std::string& path)
{
    return ::platform::esp::arduino_common::storage::sd_is_directory(path.c_str()) ||
           ::platform::esp::arduino_common::storage::sd_mkdir(path.c_str()) ||
           ::platform::esp::arduino_common::storage::sd_is_directory(path.c_str());
}

bool ensure_page_parent_dirs(const char* destination_hash, const char* path)
{
    if (!ensure_dir("/trailmate") || !ensure_dir("/trailmate/reticulum") ||
        !ensure_dir(kPagesDir))
    {
        return false;
    }

    std::string current = kPagesDir;
    current += "/";
    current += destination_hash;
    if (!ensure_dir(current))
    {
        return false;
    }

    const char* segment = path;
    while (segment && *segment != '\0')
    {
        while (*segment == '/')
        {
            ++segment;
        }
        const char* end = segment;
        while (*end != '\0' && *end != '/')
        {
            ++end;
        }
        if (*end == '\0')
        {
            return true;
        }
        if (end > segment)
        {
            current += "/";
            current.append(segment, static_cast<std::size_t>(end - segment));
            if (!ensure_dir(current))
            {
                return false;
            }
        }
        segment = end;
    }
    return true;
}

void set_request_status(Status& out,
                        const RequestStartResult& result,
                        const char* normalized_path)
{
    switch (result.code)
    {
    case RequestStartCode::Started:
        out.supported = true;
        out.request_started = true;
        set_status(out, "Nomad page request started", normalized_path);
        break;
    case RequestStartCode::AlreadyPending:
        out.supported = true;
        out.request_started = true;
        set_status(out, "Nomad page request already pending", normalized_path);
        break;
    case RequestStartCode::InvalidInput:
        out.supported = true;
        set_status(out, "Invalid Nomad page request", normalized_path);
        break;
    case RequestStartCode::Unsupported:
        set_status(out, "Nomad page fetch unavailable", normalized_path);
        break;
    case RequestStartCode::NotReady:
        out.supported = true;
        set_status(out, "Nomad page requester not ready", normalized_path);
        break;
    case RequestStartCode::Busy:
        out.supported = true;
        set_status(out, "Nomad page requester busy", normalized_path);
        break;
    case RequestStartCode::EncodeFailed:
        out.supported = true;
        set_status(out, "Cannot encode Nomad page request", normalized_path);
        break;
    case RequestStartCode::RadioTxFailed:
        out.supported = true;
        set_status(out, "Nomad page request TX failed", normalized_path);
        break;
    case RequestStartCode::StorageUnavailable:
        out.supported = true;
        set_status(out, "SD card required", kPagesDir);
        break;
    case RequestStartCode::Unknown:
    default:
        out.supported = true;
        set_status(out, "Nomad page request failed", normalized_path);
        break;
    }
}

PageCacheLoadState* allocate_page_cache_load_state()
{
    void* storage = heap_caps_malloc_prefer(sizeof(PageCacheLoadState),
                                            2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!storage)
    {
        storage = ::operator new(sizeof(PageCacheLoadState), std::nothrow);
    }
    return storage ? new (storage) PageCacheLoadState() : nullptr;
}

void page_cache_load_task_entry(void* context)
{
    auto* state = static_cast<PageCacheLoadState*>(context);
    if (!state)
    {
        vTaskDelete(nullptr);
        return;
    }

    for (;;)
    {
        uint8_t signal = 0;
        (void)xQueueReceive(state->signal, &signal, portMAX_DELAY);
        for (;;)
        {
            char destination[kReticulumPageDestinationTextSize] = {};
            char path[kReticulumPagePathSize] = {};
            if (xSemaphoreTake(state->mutex, portMAX_DELAY) != pdTRUE)
            {
                break;
            }
            if (!state->pending)
            {
                xSemaphoreGive(state->mutex);
                break;
            }
            copy_text(destination, sizeof(destination), state->pending_destination);
            copy_text(path, sizeof(path), state->pending_path);
            state->pending = false;
            state->active = true;
            state->completed = false;
            state->active_invalidation_epoch = state->invalidation_epoch;
            copy_text(state->active_destination,
                      sizeof(state->active_destination),
                      destination);
            copy_text(state->active_path, sizeof(state->active_path), path);
            set_busy_status(state->active_status,
                            "Nomad page cache loading",
                            path,
                            0);
            xSemaphoreGive(state->mutex);

            std::size_t body_len = 0;
            Status status = load_cached_page(destination,
                                             path,
                                             state->completed_body,
                                             sizeof(state->completed_body),
                                             &body_len);
            status.busy = false;
            status.progress_percent = status.loaded ? 100 : -1;

            if (xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE)
            {
                const bool publish =
                    state->active_invalidation_epoch ==
                        state->invalidation_epoch &&
                    same_page_request(state->active_destination,
                                      state->active_path,
                                      destination,
                                      path);
                state->active = false;
                if (publish)
                {
                    state->completed = true;
                    copy_text(state->completed_destination,
                              sizeof(state->completed_destination),
                              destination);
                    copy_text(state->completed_path,
                              sizeof(state->completed_path),
                              path);
                    state->completed_status = status;
                    state->completed_body_len = body_len;
                    if (body_len < sizeof(state->completed_body))
                    {
                        state->completed_body[body_len] = '\0';
                    }
                }
                else
                {
                    state->completed = false;
                    state->completed_status = {};
                    state->completed_body_len = 0;
                    state->completed_body[0] = '\0';
                }
                xSemaphoreGive(state->mutex);
            }
        }
    }
}

bool start_page_cache_load_task(PageCacheLoadState& state)
{
    if (state.task)
    {
        return true;
    }
    if (!state.mutex || !state.signal)
    {
        return false;
    }

    const BaseType_t ok = xTaskCreate(page_cache_load_task_entry,
                                      "rtpg_cache",
                                      kPageCacheAsyncTaskStackBytes,
                                      &state,
                                      kPageCacheAsyncTaskPriority,
                                      &state.task);
    if (ok != pdPASS)
    {
        state.task = nullptr;
        std::printf("[Reticulum][PageCache] async task_create_failed rc=%ld\n",
                    static_cast<long>(ok));
        return false;
    }
    return true;
}

PageCacheLoadState* ensure_page_cache_load_state()
{
    if (!s_page_cache_load_state)
    {
        s_page_cache_load_state = allocate_page_cache_load_state();
        if (s_page_cache_load_state)
        {
            std::printf("[Reticulum][PageCache] async state allocated bytes=%u\n",
                        static_cast<unsigned>(sizeof(PageCacheLoadState)));
        }
    }
    PageCacheLoadState* state = s_page_cache_load_state;
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
    (void)start_page_cache_load_task(*state);
    return state;
}

bool clear_page_cache_load_state_for_page(const char* destination,
                                          const char* path)
{
    PageCacheLoadState* state = s_page_cache_load_state;
    if (!state || !state->mutex)
    {
        return true;
    }
    if (xSemaphoreTake(state->mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return false;
    }

    bool invalidated = false;
    if (state->pending &&
        same_page_request(state->pending_destination,
                          state->pending_path,
                          destination,
                          path))
    {
        state->pending = false;
        state->pending_destination[0] = '\0';
        state->pending_path[0] = '\0';
        invalidated = true;
    }
    if (state->active &&
        same_page_request(state->active_destination,
                          state->active_path,
                          destination,
                          path))
    {
        invalidated = true;
    }
    if (state->completed &&
        same_page_request(state->completed_destination,
                          state->completed_path,
                          destination,
                          path))
    {
        state->completed = false;
        state->completed_destination[0] = '\0';
        state->completed_path[0] = '\0';
        state->completed_status = {};
        state->completed_body_len = 0;
        state->completed_body[0] = '\0';
        invalidated = true;
    }
    if (invalidated)
    {
        ++state->invalidation_epoch;
    }
    xSemaphoreGive(state->mutex);
    return true;
}

PageRequestProgressSlot* find_request_progress_slot_locked(
    const char* destination,
    const char* path,
    bool create)
{
    PageRequestProgressSlot* empty = nullptr;
    PageRequestProgressSlot* oldest = nullptr;
    for (auto& slot : s_request_progress)
    {
        if (slot.occupied &&
            same_page_request(slot.destination, slot.path, destination, path))
        {
            return &slot;
        }
        if (!slot.occupied && !empty)
        {
            empty = &slot;
        }
        if (slot.occupied && (!oldest || slot.order < oldest->order))
        {
            oldest = &slot;
        }
    }
    if (!create)
    {
        return nullptr;
    }
    PageRequestProgressSlot* slot = empty ? empty : oldest;
    if (slot)
    {
        *slot = {};
        slot->occupied = true;
        slot->order = s_request_progress_order++;
        copy_text(slot->destination, sizeof(slot->destination), destination);
        copy_text(slot->path, sizeof(slot->path), path);
    }
    return slot;
}

} // namespace

const char* cache_root_path()
{
    return kPagesDir;
}

bool normalize_path(const char* path, char* out_path, std::size_t out_len)
{
    if (!out_path || out_len == 0)
    {
        return false;
    }
    out_path[0] = '\0';

    const char* source = (path && path[0] != '\0') ? path : kDefaultPagePath;
    if (std::strcmp(source, "/") == 0)
    {
        source = kDefaultPagePath;
    }

    std::size_t written = 0;
    if (source[0] != '/')
    {
        if (written + 1U >= out_len)
        {
            return false;
        }
        out_path[written++] = '/';
    }

    bool previous_slash = false;
    for (std::size_t i = 0; source[i] != '\0'; ++i)
    {
        const char ch = source[i];
        if (!allowed_path_char(ch) || ch == '\\')
        {
            out_path[0] = '\0';
            return false;
        }
        if (ch == '/' && previous_slash)
        {
            continue;
        }
        if (written + 1U >= out_len)
        {
            out_path[0] = '\0';
            return false;
        }
        out_path[written++] = ch;
        previous_slash = ch == '/';
    }
    if (written == 0)
    {
        if (out_len <= std::strlen(kDefaultPagePath))
        {
            return false;
        }
        copy_text(out_path, out_len, kDefaultPagePath);
        return true;
    }
    if (written > 1U && out_path[written - 1U] == '/')
    {
        --written;
    }
    out_path[written] = '\0';
    if (path_has_parent_segment(out_path))
    {
        out_path[0] = '\0';
        return false;
    }
    return true;
}

void bind_request_start_handler(RequestStartHandler handler, void* context)
{
    s_request_handler = handler;
    s_request_context = context;
}

Status load_cached_page(const char* destination_hash,
                        const char* path,
                        char* out_body,
                        std::size_t body_capacity,
                        std::size_t* out_body_len)
{
    Status out{};
    out.supported = true;
    out.sd_present = page_sd_available();
    if (out_body_len)
    {
        *out_body_len = 0;
    }
    if (out_body && body_capacity != 0)
    {
        out_body[0] = '\0';
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kPagesDir);
        return out;
    }

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        !out_body || body_capacity == 0)
    {
        set_status(out, "Invalid Nomad page address", kPagesDir);
        return out;
    }

    const std::string path_text = cache_path(destination, normalized_path);
    PageCacheBusGate bus_gate(PageCacheBusAccess::Read);
    if (!bus_gate.locked())
    {
        out.busy = true;
        set_status(out, "Nomad page cache busy", path_text.c_str());
        return out;
    }

    out.file_present =
        ::platform::esp::arduino_common::storage::sd_exists(path_text.c_str()) &&
        !::platform::esp::arduino_common::storage::sd_is_directory(path_text.c_str());
    out.cache_checked = true;
    if (!out.file_present)
    {
        set_status(out, "Nomad page cache miss", path_text.c_str());
        return out;
    }

    SdRuntimeFile file;
    if (!file.open(path_text.c_str(), "r"))
    {
        set_status(out, "Cannot open Nomad page cache", path_text.c_str());
        return out;
    }
    const uint64_t size = file.size();
    const std::size_t bytes_to_read =
        size < static_cast<uint64_t>(body_capacity - 1U)
            ? static_cast<std::size_t>(size)
            : body_capacity - 1U;
    const std::size_t read = file.read_bytes(out_body, bytes_to_read);
    out_body[read] = '\0';
    if (out_body_len)
    {
        *out_body_len = read;
    }
    out.truncated = size > read;
    out.loaded = true;
    out.progress_percent = 100;
    set_status(out, "Nomad page cache loaded", path_text.c_str());
    return out;
}

Status request_cached_page_load(const char* destination_hash,
                                const char* path,
                                bool force)
{
    Status out{};
    out.supported = true;
    out.sd_present = page_sd_available();
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kPagesDir);
        return out;
    }

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        set_status(out, "Invalid Nomad page address", kPagesDir);
        return out;
    }

    PageCacheLoadState* state = ensure_page_cache_load_state();
    if (!state || !state->mutex || !state->signal)
    {
        set_status(out, "Nomad page cache worker unavailable", normalized_path);
        return out;
    }

    if (xSemaphoreTake(state->mutex, kPageCacheAsyncMutexWait) != pdTRUE)
    {
        set_busy_status(out,
                        "Nomad page cache worker busy",
                        normalized_path,
                        0);
        return out;
    }

    if (!force)
    {
        if (state->pending &&
            same_page_request(state->pending_destination,
                              state->pending_path,
                              destination,
                              normalized_path))
        {
            set_busy_status(out,
                            "Nomad page cache load queued",
                            normalized_path,
                            0);
            xSemaphoreGive(state->mutex);
            return out;
        }
        if (state->active &&
            same_page_request(state->active_destination,
                              state->active_path,
                              destination,
                              normalized_path))
        {
            out = state->active_status;
            out.busy = true;
            xSemaphoreGive(state->mutex);
            return out;
        }
        if (state->completed &&
            same_page_request(state->completed_destination,
                              state->completed_path,
                              destination,
                              normalized_path))
        {
            out = state->completed_status;
            xSemaphoreGive(state->mutex);
            return out;
        }
    }

    copy_text(state->pending_destination,
              sizeof(state->pending_destination),
              destination);
    copy_text(state->pending_path, sizeof(state->pending_path), normalized_path);
    state->pending = true;
    state->completed = false;
    set_busy_status(out,
                    "Nomad page cache load queued",
                    normalized_path,
                    0);
    xSemaphoreGive(state->mutex);

    const uint8_t signal = 1;
    (void)xQueueOverwrite(state->signal, &signal);
    return out;
}

Status poll_cached_page_load(const char* destination_hash,
                             const char* path,
                             char* out_body,
                             std::size_t body_capacity,
                             std::size_t* out_body_len)
{
    Status out{};
    out.supported = true;
    out.sd_present = page_sd_available();
    if (out_body_len)
    {
        *out_body_len = 0;
    }
    if (out_body && body_capacity != 0)
    {
        out_body[0] = '\0';
    }

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        !out_body || body_capacity == 0)
    {
        set_status(out, "Invalid Nomad page address", kPagesDir);
        return out;
    }

    PageCacheLoadState* state = ensure_page_cache_load_state();
    if (!state || !state->mutex || !state->signal)
    {
        set_status(out, "Nomad page cache worker unavailable", normalized_path);
        return out;
    }

    if (xSemaphoreTake(state->mutex, kPageCacheAsyncMutexWait) != pdTRUE)
    {
        set_busy_status(out,
                        "Nomad page cache worker busy",
                        normalized_path,
                        0);
        return out;
    }

    if (state->completed &&
        same_page_request(state->completed_destination,
                          state->completed_path,
                          destination,
                          normalized_path))
    {
        out = state->completed_status;
        const std::size_t copy_len =
            state->completed_body_len < (body_capacity - 1U)
                ? state->completed_body_len
                : body_capacity - 1U;
        if (copy_len != 0)
        {
            std::memcpy(out_body, state->completed_body, copy_len);
        }
        out_body[copy_len] = '\0';
        if (out_body_len)
        {
            *out_body_len = copy_len;
        }
        out.truncated = out.truncated || state->completed_body_len > copy_len;
        xSemaphoreGive(state->mutex);
        return out;
    }

    if ((state->pending &&
         same_page_request(state->pending_destination,
                           state->pending_path,
                           destination,
                           normalized_path)) ||
        (state->active &&
         same_page_request(state->active_destination,
                           state->active_path,
                           destination,
                           normalized_path)))
    {
        out = state->active ? state->active_status : out;
        set_busy_status(out,
                        state->active ? "Nomad page cache loading"
                                      : "Nomad page cache load queued",
                        normalized_path,
                        0);
        xSemaphoreGive(state->mutex);
        return out;
    }

    xSemaphoreGive(state->mutex);
    set_status(out, "Nomad page cache idle", normalized_path);
    return out;
}

Status store_cached_page_now(const char* destination_hash,
                             const char* path,
                             const char* body,
                             std::size_t body_len)
{
    Status out{};
    out.supported = true;
    out.sd_present = page_sd_available();
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kPagesDir);
        return out;
    }

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        (!body && body_len != 0))
    {
        set_status(out, "Invalid Nomad page address", kPagesDir);
        return out;
    }

    PageCacheBusGate bus_gate(PageCacheBusAccess::Write);
    if (!bus_gate.locked())
    {
        set_status(out, "Nomad page cache busy", kPagesDir);
        return out;
    }

    if (!ensure_page_parent_dirs(destination, normalized_path))
    {
        set_status(out, "Cannot create Nomad page cache directory", kPagesDir);
        return out;
    }

    const std::string path_text = cache_path(destination, normalized_path);
    SdRuntimeFile file;
    if (!file.open(path_text.c_str(), "w"))
    {
        set_status(out, "Cannot write Nomad page cache", path_text.c_str());
        return out;
    }
    const std::size_t written = body_len == 0 ? 0 : file.write(body, body_len);
    out.saved = written == body_len && file.flush();
    out.file_present = out.saved;
    set_status(out,
               out.saved ? "Nomad page cache saved"
                         : "Nomad page cache write failed",
               path_text.c_str());
    return out;
}

Status clear_cached_page(const char* destination_hash, const char* path)
{
    Status out{};
    out.supported = true;
    out.sd_present = page_sd_available();
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kPagesDir);
        return out;
    }

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        set_status(out, "Invalid Nomad page address", kPagesDir);
        return out;
    }

    if (!clear_page_cache_load_state_for_page(destination, normalized_path))
    {
        out.busy = true;
        set_status(out, "Nomad page cache busy", normalized_path);
        return out;
    }

    const std::string path_text = cache_path(destination, normalized_path);
    PageCacheBusGate bus_gate(PageCacheBusAccess::Write);
    if (!bus_gate.locked())
    {
        out.busy = true;
        set_status(out, "Nomad page cache busy", path_text.c_str());
        return out;
    }

    out.cache_checked = true;
    out.file_present =
        ::platform::esp::arduino_common::storage::sd_exists(path_text.c_str()) &&
        !::platform::esp::arduino_common::storage::sd_is_directory(path_text.c_str());
    const bool file_existed = out.file_present;
    bool removed = true;
    if (file_existed)
    {
        removed = ::platform::esp::arduino_common::storage::sd_remove(
            path_text.c_str());
    }
    out.file_present = file_existed && !removed;
    set_status(out,
               !file_existed
                   ? "Nomad page cache already clear"
                   : (removed ? "Nomad page cache cleared"
                              : "Cannot clear Nomad page cache"),
               path_text.c_str());
    return out;
}

Status request_page(const char* destination_hash, const char* path)
{
    Status out{};
    out.supported = s_request_handler != nullptr;
    out.sd_present = page_sd_available();
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        set_status(out, "Invalid Nomad page address", kPagesDir);
        return out;
    }

    if (!s_request_handler)
    {
        set_status(out, "Nomad page fetch unavailable", normalized_path);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kPagesDir);
        return out;
    }

    uint8_t destination_bytes[kReticulumPageDestinationTextSize / 2U] = {};
    if (!destination_to_bytes(destination, destination_bytes))
    {
        set_status(out, "Invalid Nomad page address", kPagesDir);
        return out;
    }

    const RequestStartResult result =
        s_request_handler(destination_bytes, normalized_path, s_request_context);
    set_request_status(out, result, normalized_path);
    return out;
}

RequestProgress get_request_progress(const char* destination_hash,
                                     const char* path)
{
    RequestProgress out{};
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        copy_text(out.message, sizeof(out.message), "Invalid Nomad page address");
        return out;
    }

    taskENTER_CRITICAL(&s_request_progress_lock);
    const PageRequestProgressSlot* slot =
        find_request_progress_slot_locked(destination, normalized_path, false);
    if (slot)
    {
        out = slot->progress;
    }
    taskEXIT_CRITICAL(&s_request_progress_lock);
    return out;
}

void update_request_progress(const char* destination_hash,
                             const char* path,
                             int progress_percent,
                             const char* message,
                             const char* detail,
                             bool active,
                             bool complete,
                             RequestProgress::FailureKind failure)
{
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        return;
    }
    if (progress_percent > 100)
    {
        progress_percent = 100;
    }
    if (progress_percent < -1)
    {
        progress_percent = -1;
    }

    RequestProgress progress{};
    progress.active = active;
    progress.complete = complete;
    progress.failure = failure;
    progress.progress_percent = progress_percent;
    copy_text(progress.message, sizeof(progress.message), message);
    copy_text(progress.detail, sizeof(progress.detail), detail);

    taskENTER_CRITICAL(&s_request_progress_lock);
    PageRequestProgressSlot* slot =
        find_request_progress_slot_locked(destination, normalized_path, true);
    if (slot)
    {
        slot->progress = progress;
        slot->order = s_request_progress_order++;
    }
    taskEXIT_CRITICAL(&s_request_progress_lock);
}

void clear_request_progress(const char* destination_hash, const char* path)
{
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        return;
    }

    taskENTER_CRITICAL(&s_request_progress_lock);
    PageRequestProgressSlot* slot =
        find_request_progress_slot_locked(destination, normalized_path, false);
    if (slot)
    {
        *slot = {};
    }
    taskEXIT_CRITICAL(&s_request_progress_lock);
}

} // namespace platform::ui::reticulum_page
