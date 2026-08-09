#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_14
#endif

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define UI_PACKET_PROBE_STATE_RAM_ATTR EXT_RAM_ATTR
#else
#define UI_PACKET_PROBE_STATE_RAM_ATTR
#endif

using Host = energy_sweep::ui::shell::Host;

namespace
{

constexpr lv_coord_t kPagerWidth = 480;
constexpr lv_coord_t kPagerHeight = 222;
constexpr lv_coord_t kPagerTopBarHeight = 30;
constexpr lv_coord_t kPagerBottomBarHeight = 24;
constexpr lv_coord_t kPagerOuterMargin = 10;
constexpr int kMaxCandidates = 96;
constexpr int kMaxObservations = 8;
constexpr std::size_t kPacketScratchSize = 255;
constexpr uint32_t kRefreshIntervalMs = 35;
constexpr uint32_t kCandidateDwellMs = 700;
constexpr float kDefaultFreqStartMhz = 433.050f;
constexpr float kDefaultFreqEndMhz = 434.790f;
constexpr float kStepQuantMhz = 0.025f;
constexpr int kTargetCandidateCount = 70;

constexpr uint32_t kColorAmber = 0xEBA341;
constexpr uint32_t kColorAmberDark = 0xC98118;
constexpr uint32_t kColorWarmBg = 0xFFF3DF;
constexpr uint32_t kColorPanelBg = 0xF8E6C3;
constexpr uint32_t kColorLine = 0xB3915D;
constexpr uint32_t kColorText = 0x593D1C;
constexpr uint32_t kColorTextDim = 0x846A42;
constexpr uint32_t kColorOk = 0x397046;
constexpr uint32_t kColorWarning = 0xA6422A;
constexpr uint32_t kColorInfo = 0x315F91;
constexpr uint32_t kColorFocus = 0xD59A36;

const Host* s_host = nullptr;

struct AirProfile
{
    float frequency_mhz = 0.0f;
    float bw_khz = 125.0f;
    uint8_t sf = 11;
    uint8_t cr = 5;
    int8_t tx_power = 14;
    uint16_t preamble_len = 16;
    uint8_t sync_word = 0x12;
    uint8_t crc_len = 2;
};

struct ProbeBandPlan
{
    float freq_start_mhz = kDefaultFreqStartMhz;
    float freq_end_mhz = kDefaultFreqEndMhz;
    float step_mhz = kStepQuantMhz;
    int candidate_count = 2;
};

struct RadioContext
{
    bool supported_protocol = false;
    bool acquired = false;
    chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
    AirProfile base_profile{};
};

struct ProbeObservation
{
    AirProfile profile{};
    uint32_t evidence_count = 0;
    uint32_t first_seen_ms = 0;
    uint32_t last_seen_ms = 0;
    float last_rssi_dbm = 0.0f;
    float last_snr_db = 0.0f;
};

struct ProbeState
{
    bool scanning = false;
    bool radio_error = false;
    bool applied = false;
    bool confirmation_open = false;
    bool confirm_apply_selected = false;
    int candidate_index = 0;
    int checked_in_pass = 0;
    uint32_t completed_passes = 0;
    uint32_t candidate_started_ms = 0;
    int selected_observation = 0;
    int visible_page_start = 0;
    int observation_count = 0;
    std::array<ProbeObservation, kMaxObservations> observations{};
    std::array<uint8_t, kPacketScratchSize> receive_scratch{};
    std::array<uint8_t, kPacketScratchSize> protocol_scratch{};
};

struct PacketProbeLayout
{
    lv_coord_t screen_w = kPagerWidth;
    lv_coord_t screen_h = kPagerHeight;
    lv_coord_t topbar_h = kPagerTopBarHeight;
    lv_coord_t bottom_bar_h = kPagerBottomBarHeight;
    lv_coord_t work_top = kPagerTopBarHeight;
    lv_coord_t work_bottom = kPagerHeight - kPagerBottomBarHeight;
    lv_coord_t content_x = kPagerOuterMargin;
    lv_coord_t content_w = kPagerWidth - (kPagerOuterMargin * 2);
    bool pager = true;
};

struct PacketProbeUi
{
    lv_obj_t* root = nullptr;
    ::ui::widgets::TopBar top_bar = {};
    lv_obj_t* content_area = nullptr;
    lv_obj_t* state_label = nullptr;
    lv_obj_t* observed_label = nullptr;
    lv_obj_t* empty_label = nullptr;
    std::array<lv_obj_t*, kMaxObservations> result_rows{};
    std::array<lv_obj_t*, kMaxObservations> result_primary{};
    std::array<lv_obj_t*, kMaxObservations> result_secondary{};
    std::array<lv_obj_t*, kMaxObservations> result_count{};
    lv_obj_t* progress_label = nullptr;
    lv_obj_t* bottom_bar = nullptr;
    lv_obj_t* stop_button = nullptr;
    lv_obj_t* stop_label = nullptr;
    lv_obj_t* set_button = nullptr;
    lv_obj_t* set_label = nullptr;
    lv_obj_t* confirmation = nullptr;
    lv_obj_t* confirm_cancel = nullptr;
    lv_obj_t* confirm_apply = nullptr;
};

UI_PACKET_PROBE_STATE_RAM_ATTR PacketProbeUi s_ui;
UI_PACKET_PROBE_STATE_RAM_ATTR ProbeState s_state;
UI_PACKET_PROBE_STATE_RAM_ATTR RadioContext s_radio;
UI_PACKET_PROBE_STATE_RAM_ATTR ProbeBandPlan s_band;
UI_PACKET_PROBE_STATE_RAM_ATTR PacketProbeLayout s_layout;
lv_timer_t* s_refresh_timer = nullptr;

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

int active_candidate_count()
{
    return std::clamp(s_band.candidate_count, 2, kMaxCandidates);
}

int clamp_observation_index(int index)
{
    if (s_state.observation_count <= 0)
    {
        return 0;
    }
    return std::clamp(index, 0, s_state.observation_count - 1);
}

int visible_result_capacity()
{
    return s_layout.pager ? 4 : kMaxObservations;
}

float candidate_frequency_mhz(int index)
{
    const int clamped = std::clamp(index, 0, active_candidate_count() - 1);
    return s_band.freq_start_mhz + static_cast<float>(clamped) * s_band.step_mhz;
}

PacketProbeLayout resolve_layout(lv_obj_t* parent)
{
    if (parent)
    {
        lv_obj_update_layout(parent);
    }

    PacketProbeLayout layout{};
    const lv_coord_t parent_w = parent ? lv_obj_get_width(parent) : 0;
    const lv_coord_t parent_h = parent ? lv_obj_get_height(parent) : 0;
    const bool pager = parent_w <= 0 || parent_h <= 0 ||
                       (parent_w <= 600 && parent_h <= 320);
    layout.pager = pager;
    layout.screen_w = pager ? kPagerWidth : parent_w;
    layout.screen_h = pager ? kPagerHeight : parent_h;
    layout.topbar_h = pager ? kPagerTopBarHeight
                            : std::max<lv_coord_t>(42, ::ui::page_profile::current().top_bar_height);
    layout.bottom_bar_h = pager ? kPagerBottomBarHeight : 34;
    layout.work_top = layout.topbar_h;
    layout.work_bottom = layout.screen_h - layout.bottom_bar_h;

    const lv_coord_t margin = pager ? kPagerOuterMargin : 18;
    layout.content_x = margin;
    layout.content_w = layout.screen_w - (margin * 2);
    return layout;
}

platform::ui::lora::ReceiveConfig receive_config_for(const AirProfile& profile)
{
    platform::ui::lora::ReceiveConfig config{};
    config.bw_khz = profile.bw_khz;
    config.sf = profile.sf;
    config.cr = profile.cr;
    config.tx_power = profile.tx_power;
    config.preamble_len = profile.preamble_len;
    config.sync_word = profile.sync_word;
    config.crc_len = profile.crc_len;
    return config;
}

const chat::meshtastic::RegionInfo* find_region_for_frequency(float frequency_mhz)
{
    size_t count = 0;
    const auto* regions = chat::meshtastic::getRegionTable(&count);
    const chat::meshtastic::RegionInfo* nearest = nullptr;
    float nearest_distance = std::numeric_limits<float>::max();
    for (size_t index = 0; regions && index < count; ++index)
    {
        const auto& region = regions[index];
        if (region.code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            continue;
        }
        const float distance = frequency_mhz < region.freq_start_mhz
                                   ? region.freq_start_mhz - frequency_mhz
                                   : (frequency_mhz > region.freq_end_mhz
                                          ? frequency_mhz - region.freq_end_mhz
                                          : 0.0f);
        if (!nearest || distance < nearest_distance)
        {
            nearest = &region;
            nearest_distance = distance;
        }
    }
    return nearest ? nearest
                   : chat::meshtastic::findRegion(meshtastic_Config_LoRaConfig_RegionCode_CN);
}

void setup_band_plan(float start_mhz, float end_mhz, float bandwidth_khz)
{
    if (!std::isfinite(start_mhz) || !std::isfinite(end_mhz) || start_mhz <= 0.0f ||
        end_mhz <= 0.0f)
    {
        start_mhz = kDefaultFreqStartMhz;
        end_mhz = kDefaultFreqEndMhz;
    }
    if (end_mhz < start_mhz)
    {
        std::swap(start_mhz, end_mhz);
    }

    const float safe_bw_khz = (std::isfinite(bandwidth_khz) && bandwidth_khz > 1.0f)
                                  ? bandwidth_khz
                                  : 125.0f;
    const float half_bandwidth_mhz = safe_bw_khz / 2000.0f;
    if ((end_mhz - start_mhz) > (2.0f * half_bandwidth_mhz))
    {
        start_mhz += half_bandwidth_mhz;
        end_mhz -= half_bandwidth_mhz;
    }

    const float span_mhz = std::max(kStepQuantMhz, end_mhz - start_mhz);
    float step_mhz = span_mhz / static_cast<float>(kTargetCandidateCount - 1);
    step_mhz = std::max(kStepQuantMhz, std::ceil(step_mhz / kStepQuantMhz) * kStepQuantMhz);
    int candidates = static_cast<int>(std::floor(span_mhz / step_mhz)) + 1;
    while (candidates > kMaxCandidates)
    {
        step_mhz += kStepQuantMhz;
        candidates = static_cast<int>(std::floor(span_mhz / step_mhz)) + 1;
    }

    s_band.freq_start_mhz = start_mhz;
    s_band.freq_end_mhz = start_mhz + step_mhz * static_cast<float>(std::max(1, candidates - 1));
    s_band.step_mhz = step_mhz;
    s_band.candidate_count = std::max(2, candidates);
}

void setup_radio_context()
{
    s_radio = {};
    s_band = {};

    const app::AppConfig& config = app::configFacade().readConfig();
    s_radio.protocol = config.mesh_protocol;
    if (config.mesh_protocol == chat::MeshProtocol::Meshtastic)
    {
        const chat::meshtastic::RadioConfig radio =
            chat::meshtastic::deriveRadioConfig(config.meshtastic_config);
        s_radio.supported_protocol = true;
        s_radio.base_profile.bw_khz = radio.bw_khz;
        s_radio.base_profile.sf = radio.sf;
        s_radio.base_profile.cr = radio.cr_denom;
        s_radio.base_profile.tx_power = radio.tx_power_dbm;
        s_radio.base_profile.preamble_len = radio.preamble_len;
        s_radio.base_profile.sync_word = radio.sync_word;
        s_radio.base_profile.crc_len = radio.crc_len;

        const auto* region = chat::meshtastic::findRegion(radio.region_code);
        if (region)
        {
            setup_band_plan(region->freq_start_mhz, region->freq_end_mhz, radio.bw_khz);
        }
        else
        {
            setup_band_plan(kDefaultFreqStartMhz, kDefaultFreqEndMhz, radio.bw_khz);
        }
        return;
    }

    if (config.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        const chat::MeshConfig& mesh = config.meshcore_config;
        s_radio.supported_protocol = true;
        s_radio.base_profile.bw_khz = mesh.meshcore_bw_khz;
        s_radio.base_profile.sf = std::clamp<uint8_t>(mesh.meshcore_sf, 5, 12);
        s_radio.base_profile.cr = std::clamp<uint8_t>(mesh.meshcore_cr, 5, 8);
        s_radio.base_profile.tx_power = mesh.tx_power;
        s_radio.base_profile.preamble_len = 16;
        s_radio.base_profile.sync_word = 0x12;
        s_radio.base_profile.crc_len = 2;

        float hint_frequency = mesh.meshcore_freq_mhz;
        if (mesh.meshcore_region_preset > 0)
        {
            if (const auto* preset =
                    chat::meshcore::findRegionPresetById(mesh.meshcore_region_preset))
            {
                hint_frequency = preset->freq_mhz;
            }
        }
        if (const auto* region = find_region_for_frequency(hint_frequency))
        {
            setup_band_plan(region->freq_start_mhz, region->freq_end_mhz,
                            s_radio.base_profile.bw_khz);
        }
        else
        {
            setup_band_plan(kDefaultFreqStartMhz, kDefaultFreqEndMhz,
                            s_radio.base_profile.bw_khz);
        }
    }
}

AirProfile current_candidate()
{
    AirProfile profile = s_radio.base_profile;
    profile.frequency_mhz = candidate_frequency_mhz(s_state.candidate_index);
    return profile;
}

bool acquire_radio_runtime()
{
    if (!platform::ui::lora::acquire() || !platform::ui::lora::is_online())
    {
        return false;
    }
    s_radio.acquired = true;
    return true;
}

void release_radio_runtime()
{
    if (!s_radio.acquired)
    {
        return;
    }
    platform::ui::lora::release();
    s_radio.acquired = false;
}

bool configure_current_candidate()
{
    const AirProfile profile = current_candidate();
    if (!platform::ui::lora::configure_receive(profile.frequency_mhz,
                                               receive_config_for(profile)))
    {
        return false;
    }
    s_state.candidate_started_ms = sys::millis_now();
    return true;
}

bool validate_protocol_packet(const uint8_t* data, std::size_t size)
{
    if (!data || size == 0)
    {
        return false;
    }

    if (s_radio.protocol == chat::MeshProtocol::Meshtastic)
    {
        chat::meshtastic::PacketHeaderWire header{};
        std::size_t payload_size = s_state.protocol_scratch.size();
        return chat::meshtastic::parseWirePacket(data,
                                                 size,
                                                 &header,
                                                 s_state.protocol_scratch.data(),
                                                 &payload_size) &&
               header.id != 0 && header.from != 0 && payload_size > 0;
    }

    if (s_radio.protocol == chat::MeshProtocol::MeshCore)
    {
        chat::meshcore::ParsedPacket packet{};
        return chat::meshcore::parsePacket(data, size, &packet) && packet.payload != nullptr &&
               packet.payload_len > 0;
    }

    return false;
}

bool same_profile(const AirProfile& lhs, const AirProfile& rhs)
{
    return std::fabs(lhs.frequency_mhz - rhs.frequency_mhz) < 0.0005f &&
           std::fabs(lhs.bw_khz - rhs.bw_khz) < 0.01f && lhs.sf == rhs.sf &&
           lhs.cr == rhs.cr && lhs.preamble_len == rhs.preamble_len &&
           lhs.sync_word == rhs.sync_word && lhs.crc_len == rhs.crc_len;
}

void restore_page_focus();

void record_observation(const AirProfile& profile,
                        const platform::ui::lora::ReceivedPacket& packet)
{
    int observation_index = -1;
    bool is_new_observation = false;
    for (int index = 0; index < s_state.observation_count; ++index)
    {
        if (same_profile(s_state.observations[index].profile, profile))
        {
            observation_index = index;
            break;
        }
    }
    if (observation_index < 0)
    {
        if (s_state.observation_count >= kMaxObservations)
        {
            return;
        }
        observation_index = s_state.observation_count++;
        is_new_observation = true;
        s_state.observations[observation_index] = {};
        s_state.observations[observation_index].profile = profile;
        s_state.observations[observation_index].first_seen_ms = sys::millis_now();
    }

    ProbeObservation& observation = s_state.observations[observation_index];
    observation.evidence_count++;
    observation.last_seen_ms = sys::millis_now();
    observation.last_rssi_dbm = packet.rssi_dbm;
    observation.last_snr_db = packet.snr_db;
    if (s_state.observation_count == 1)
    {
        s_state.selected_observation = 0;
    }
    if (is_new_observation && s_ui.root)
    {
        restore_page_focus();
    }
}

void stop_probe()
{
    s_state.scanning = false;
    release_radio_runtime();
}

void start_probe()
{
    s_state.applied = false;
    s_state.radio_error = false;
    if (!s_radio.supported_protocol || !platform::ui::lora::is_supported() ||
        !acquire_radio_runtime())
    {
        s_state.radio_error = true;
        return;
    }

    s_state.candidate_index = 0;
    s_state.checked_in_pass = 0;
    s_state.candidate_started_ms = 0;
    if (!configure_current_candidate())
    {
        s_state.radio_error = true;
        release_radio_runtime();
        return;
    }
    s_state.scanning = true;
}

void process_scan_step()
{
    if (!s_state.scanning)
    {
        return;
    }

    for (int packet_index = 0; packet_index < 2; ++packet_index)
    {
        platform::ui::lora::ReceivedPacket packet{};
        if (!platform::ui::lora::poll_received_packet(s_state.receive_scratch.data(),
                                                      s_state.receive_scratch.size(),
                                                      &packet))
        {
            break;
        }
        if (validate_protocol_packet(s_state.receive_scratch.data(), packet.size))
        {
            record_observation(current_candidate(), packet);
        }
    }

    const uint32_t now = sys::millis_now();
    if ((now - s_state.candidate_started_ms) < kCandidateDwellMs)
    {
        return;
    }

    s_state.checked_in_pass++;
    s_state.candidate_index++;
    if (s_state.candidate_index >= active_candidate_count())
    {
        s_state.candidate_index = 0;
        s_state.checked_in_pass = 0;
        s_state.completed_passes++;
    }
    if (!configure_current_candidate())
    {
        s_state.radio_error = true;
        stop_probe();
    }
}

void format_profile_params(const AirProfile& profile, char* buffer, std::size_t buffer_size)
{
    const float rounded_bandwidth = std::round(profile.bw_khz);
    if (std::fabs(profile.bw_khz - rounded_bandwidth) < 0.01f)
    {
        snprintf(buffer,
                 buffer_size,
                 "BW %.0fk  SF%u  CR4/%u",
                 static_cast<double>(rounded_bandwidth),
                 static_cast<unsigned>(profile.sf),
                 static_cast<unsigned>(profile.cr));
        return;
    }
    snprintf(buffer,
             buffer_size,
             "BW %.1fk  SF%u  CR4/%u",
             static_cast<double>(profile.bw_khz),
             static_cast<unsigned>(profile.sf),
             static_cast<unsigned>(profile.cr));
}

void refresh_rows()
{
    const int visible_rows = visible_result_capacity();
    for (int visual_index = 0; visual_index < kMaxObservations; ++visual_index)
    {
        lv_obj_t* row = s_ui.result_rows[visual_index];
        if (!row)
        {
            continue;
        }
        const int observation_index = s_state.visible_page_start + visual_index;
        if (visual_index >= visible_rows || observation_index >= s_state.observation_count)
        {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        const ProbeObservation& observation = s_state.observations[observation_index];
        char primary[32];
        char secondary[40];
        char count[16];
        snprintf(primary, sizeof(primary), "%.3f MHz", observation.profile.frequency_mhz);
        format_profile_params(observation.profile, secondary, sizeof(secondary));
        snprintf(count, sizeof(count), "x%lu", static_cast<unsigned long>(observation.evidence_count));
        lv_label_set_text(s_ui.result_primary[visual_index], primary);
        lv_label_set_text(s_ui.result_secondary[visual_index], secondary);
        lv_label_set_text(s_ui.result_count[visual_index], count);

        const bool selected = observation_index == clamp_observation_index(s_state.selected_observation);
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(selected ? kColorAmber : kColorPanelBg),
                                  0);
        lv_obj_set_style_border_color(row,
                                      lv_color_hex(selected ? kColorAmberDark : kColorLine),
                                      0);
        lv_obj_set_style_text_color(s_ui.result_primary[visual_index],
                                    lv_color_hex(selected ? kColorText : kColorText),
                                    0);
        lv_obj_set_style_text_color(s_ui.result_secondary[visual_index],
                                    lv_color_hex(selected ? kColorText : kColorTextDim),
                                    0);
        lv_obj_set_style_text_color(s_ui.result_count[visual_index],
                                    lv_color_hex(selected ? kColorText : kColorOk),
                                    0);
    }

    if (s_ui.empty_label)
    {
        if (s_state.observation_count == 0)
        {
            lv_obj_clear_flag(s_ui.empty_label, LV_OBJ_FLAG_HIDDEN);
            ::ui::i18n::set_label_text(s_ui.empty_label,
                                       s_state.scanning ? "NO VALIDATED PACKETS" : "NO OBSERVED PROFILES");
        }
        else
        {
            lv_obj_add_flag(s_ui.empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void refresh_status()
{
    if (!s_ui.state_label)
    {
        return;
    }

    char text[48];
    uint32_t color = kColorTextDim;
    if (!s_radio.supported_protocol)
    {
        snprintf(text, sizeof(text), "UNSUPPORTED PROTOCOL");
        color = kColorWarning;
    }
    else if (s_state.radio_error)
    {
        snprintf(text, sizeof(text), "RADIO UNAVAILABLE");
        color = kColorWarning;
    }
    else if (s_state.applied)
    {
        snprintf(text, sizeof(text), "APPLIED TO MESH");
        color = kColorOk;
    }
    else if (s_state.scanning)
    {
        snprintf(text,
                 sizeof(text),
                 "LISTENING  %d/%d  PASS %lu",
                 s_state.checked_in_pass + 1,
                 active_candidate_count(),
                 static_cast<unsigned long>(s_state.completed_passes + 1));
        color = kColorInfo;
    }
    else
    {
        snprintf(text, sizeof(text), "READY  1 SF LANE");
    }
    lv_label_set_text(s_ui.state_label, text);
    lv_obj_set_style_text_color(s_ui.state_label, lv_color_hex(color), 0);

    if (s_ui.progress_label)
    {
        snprintf(text,
                 sizeof(text),
                 "%d/%d PROFILES  %d OBSERVED",
                 s_state.scanning ? s_state.checked_in_pass + 1 : 0,
                 active_candidate_count(),
                 s_state.observation_count);
        lv_label_set_text(s_ui.progress_label, text);
    }

    if (s_ui.stop_label)
    {
        ::ui::i18n::set_label_text(s_ui.stop_label, s_state.scanning ? "S  STOP" : "S  START");
    }
}

void refresh_selected_profile()
{
    const bool has_selection = s_state.observation_count > 0;
    if (s_ui.set_button && s_ui.set_label)
    {
        const uint32_t color = has_selection ? kColorAmber : kColorLine;
        lv_obj_set_style_bg_color(s_ui.set_button, lv_color_hex(has_selection ? kColorPanelBg : kColorWarmBg), 0);
        lv_obj_set_style_border_color(s_ui.set_button, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(s_ui.set_label, lv_color_hex(has_selection ? kColorText : kColorTextDim), 0);
    }
}

void refresh_all_ui()
{
    ui_update_top_bar_battery(s_ui.top_bar);
    refresh_status();
    refresh_rows();
    refresh_selected_profile();
}

void refresh_timer_cb(lv_timer_t*)
{
    if (!s_ui.root)
    {
        return;
    }
    process_scan_step();
    refresh_all_ui();
}

void restore_page_focus()
{
    if (!::app_g)
    {
        return;
    }

    lv_group_remove_all_objs(::app_g);
    if (s_ui.top_bar.back_btn)
    {
        lv_group_add_obj(::app_g, s_ui.top_bar.back_btn);
    }
    const int visible_count = std::min(
        visible_result_capacity(),
        std::max(0, s_state.observation_count - s_state.visible_page_start));
    for (int index = 0; index < visible_count; ++index)
    {
        if (s_ui.result_rows[index])
        {
            lv_group_add_obj(::app_g, s_ui.result_rows[index]);
        }
    }
    if (s_ui.stop_button)
    {
        lv_group_add_obj(::app_g, s_ui.stop_button);
    }
    if (s_ui.set_button)
    {
        lv_group_add_obj(::app_g, s_ui.set_button);
    }
    if (s_ui.top_bar.back_btn)
    {
        lv_group_focus_obj(s_ui.top_bar.back_btn);
    }
    set_default_group(::app_g);
    lv_group_set_editing(::app_g, false);
}

void close_confirmation();
void control_key_event_cb(lv_event_t* event);

void on_confirm_cancel_clicked(lv_event_t*)
{
    close_confirmation();
}

bool apply_selected_profile()
{
    if (s_state.observation_count <= 0)
    {
        return false;
    }

    const AirProfile profile =
        s_state.observations[clamp_observation_index(s_state.selected_observation)].profile;
    stop_probe();

    app::IAppFacade& app_ctx = app::appFacade();
    auto edit = app_ctx.beginConfigEdit();
    if (!edit || edit.config().mesh_protocol != s_radio.protocol)
    {
        s_state.radio_error = true;
        return false;
    }

    if (s_radio.protocol == chat::MeshProtocol::Meshtastic)
    {
        chat::MeshConfig& mesh = edit.config().meshtastic_config;
        mesh.use_preset = false;
        mesh.bandwidth_khz = profile.bw_khz;
        mesh.spread_factor = profile.sf;
        mesh.coding_rate = profile.cr;
        mesh.override_frequency_mhz = profile.frequency_mhz;
        mesh.frequency_offset_mhz = 0.0f;
    }
    else if (s_radio.protocol == chat::MeshProtocol::MeshCore)
    {
        chat::MeshConfig& mesh = edit.config().meshcore_config;
        mesh.meshcore_region_preset = 0;
        mesh.meshcore_freq_mhz = profile.frequency_mhz;
        mesh.meshcore_bw_khz = profile.bw_khz;
        mesh.meshcore_sf = profile.sf;
        mesh.meshcore_cr = profile.cr;
    }
    else
    {
        return false;
    }

    edit.commit(app::AppConfigChangeSet::mesh());
    app_ctx.applyMeshConfig();
    s_state.applied = true;
    s_state.radio_error = false;
    return true;
}

void on_confirm_apply_clicked(lv_event_t*)
{
    close_confirmation();
    (void)apply_selected_profile();
    refresh_all_ui();
}

void open_confirmation()
{
    if (s_state.confirmation_open || s_state.observation_count <= 0 || !s_ui.root)
    {
        return;
    }

    s_state.confirmation_open = true;
    s_state.confirm_apply_selected = false;
    s_ui.confirmation = lv_obj_create(s_ui.root);
    lv_obj_set_size(s_ui.confirmation, s_layout.screen_w, s_layout.screen_h);
    lv_obj_set_pos(s_ui.confirmation, 0, 0);
    lv_obj_set_style_bg_color(s_ui.confirmation, lv_color_hex(0x3A2A1A), 0);
    lv_obj_set_style_bg_opa(s_ui.confirmation, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_ui.confirmation, 0, 0);
    lv_obj_set_style_radius(s_ui.confirmation, 0, 0);
    lv_obj_set_style_pad_all(s_ui.confirmation, 0, 0);
    lv_obj_clear_flag(s_ui.confirmation, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t panel_w = s_layout.pager ? 236 : 340;
    const lv_coord_t panel_h = s_layout.pager ? 126 : 168;
    lv_obj_t* panel = lv_obj_create(s_ui.confirmation);
    lv_obj_set_size(panel, panel_w, panel_h);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(kColorAmberDark), 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, s_layout.pager ? 8 : 14, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(panel);
    ::ui::i18n::set_label_text(title, "APPLY OBSERVED PROFILE?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(title, 8, 8);

    const ProbeObservation& observation =
        s_state.observations[clamp_observation_index(s_state.selected_observation)];
    char frequency[32];
    char params[40];
    snprintf(frequency, sizeof(frequency), "%.3f MHz", observation.profile.frequency_mhz);
    format_profile_params(observation.profile, params, sizeof(params));

    lv_obj_t* detail = lv_label_create(panel);
    lv_label_set_text(detail, frequency);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(kColorInfo), 0);
    lv_obj_set_pos(detail, 8, 30);

    lv_obj_t* detail_params = lv_label_create(panel);
    lv_label_set_text(detail_params, params);
    lv_obj_set_style_text_font(detail_params, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detail_params, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(detail_params, 8, 50);

    const lv_coord_t button_y = panel_h - (s_layout.pager ? 32 : 46);
    const lv_coord_t button_h = s_layout.pager ? 24 : 34;
    const lv_coord_t button_w = (panel_w - 24) / 2;
    s_ui.confirm_cancel = lv_btn_create(panel);
    lv_obj_set_pos(s_ui.confirm_cancel, 8, button_y);
    lv_obj_set_size(s_ui.confirm_cancel, button_w, button_h);
    lv_obj_set_style_bg_color(s_ui.confirm_cancel, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.confirm_cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.confirm_cancel, 1, 0);
    lv_obj_set_style_border_color(s_ui.confirm_cancel, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.confirm_cancel, 4, 0);

    lv_obj_t* cancel_label = lv_label_create(s_ui.confirm_cancel);
    ::ui::i18n::set_label_text(cancel_label, "ESC  CANCEL");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(kColorText), 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(s_ui.confirm_cancel,
                        on_confirm_cancel_clicked,
                        LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_add_event_cb(s_ui.confirm_cancel, control_key_event_cb, LV_EVENT_KEY, nullptr);

    s_ui.confirm_apply = lv_btn_create(panel);
    lv_obj_set_pos(s_ui.confirm_apply, panel_w - button_w - 8, button_y);
    lv_obj_set_size(s_ui.confirm_apply, button_w, button_h);
    lv_obj_set_style_bg_color(s_ui.confirm_apply, lv_color_hex(kColorAmber), 0);
    lv_obj_set_style_bg_opa(s_ui.confirm_apply, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.confirm_apply, 1, 0);
    lv_obj_set_style_border_color(s_ui.confirm_apply, lv_color_hex(kColorAmberDark), 0);
    lv_obj_set_style_radius(s_ui.confirm_apply, 4, 0);

    lv_obj_t* apply_label = lv_label_create(s_ui.confirm_apply);
    ::ui::i18n::set_label_text(apply_label, "ENTER  APPLY");
    lv_obj_set_style_text_font(apply_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(apply_label, lv_color_hex(kColorText), 0);
    lv_obj_center(apply_label);
    lv_obj_add_event_cb(s_ui.confirm_apply,
                        on_confirm_apply_clicked,
                        LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_add_event_cb(s_ui.confirm_apply, control_key_event_cb, LV_EVENT_KEY, nullptr);

    if (::app_g)
    {
        lv_group_remove_all_objs(::app_g);
        lv_group_add_obj(::app_g, s_ui.confirm_cancel);
        lv_group_add_obj(::app_g, s_ui.confirm_apply);
        lv_group_focus_obj(s_ui.confirm_cancel);
        set_default_group(::app_g);
        lv_group_set_editing(::app_g, false);
    }
}

void close_confirmation()
{
    if (s_ui.confirmation)
    {
        lv_obj_del(s_ui.confirmation);
    }
    s_ui.confirmation = nullptr;
    s_ui.confirm_cancel = nullptr;
    s_ui.confirm_apply = nullptr;
    s_state.confirmation_open = false;
    s_state.confirm_apply_selected = false;
    restore_page_focus();
}

void handle_confirmation_key(uint32_t key)
{
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        close_confirmation();
        return;
    }
    if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT)
    {
        s_state.confirm_apply_selected = !s_state.confirm_apply_selected;
        if (::app_g)
        {
            lv_group_focus_obj(s_state.confirm_apply_selected ? s_ui.confirm_apply
                                                              : s_ui.confirm_cancel);
        }
        return;
    }
    if (key == LV_KEY_ENTER)
    {
        const bool should_apply = s_state.confirm_apply_selected;
        close_confirmation();
        if (should_apply)
        {
            (void)apply_selected_profile();
            refresh_all_ui();
        }
    }
}

void select_observation(int index)
{
    if (s_state.observation_count <= 0)
    {
        return;
    }
    s_state.selected_observation = clamp_observation_index(index);
    const int visible_rows = visible_result_capacity();
    s_state.visible_page_start =
        (s_state.selected_observation / visible_rows) * visible_rows;
    refresh_all_ui();
}

void on_result_row_clicked(lv_event_t* event)
{
    const auto visual_index =
        static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    select_observation(s_state.visible_page_start + visual_index);
}

void on_stop_clicked(lv_event_t*)
{
    if (s_state.scanning)
    {
        stop_probe();
    }
    else
    {
        start_probe();
    }
    refresh_all_ui();
}

void on_set_clicked(lv_event_t*)
{
    open_confirmation();
}

void on_back_requested(lv_event_t*)
{
    if (s_state.confirmation_open)
    {
        close_confirmation();
        return;
    }
    request_exit();
}

void top_bar_back_requested(void*)
{
    on_back_requested(nullptr);
}

void handle_key_common(uint32_t key)
{
    if (s_state.confirmation_open)
    {
        handle_confirmation_key(key);
        return;
    }

    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        on_back_requested(nullptr);
        return;
    }
    if (key == LV_KEY_UP)
    {
        select_observation(s_state.selected_observation - 1);
        return;
    }
    if (key == LV_KEY_DOWN)
    {
        select_observation(s_state.selected_observation + 1);
        return;
    }
    if (key == LV_KEY_ENTER)
    {
        open_confirmation();
        return;
    }
    if (key == 's' || key == 'S')
    {
        on_stop_clicked(nullptr);
    }
}

void root_key_event_cb(lv_event_t* event)
{
    handle_key_common(lv_event_get_key(event));
}

void control_key_event_cb(lv_event_t* event)
{
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ENTER)
    {
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (target == s_ui.top_bar.back_btn)
        {
            on_back_requested(nullptr);
        }
        else if (target == s_ui.stop_button)
        {
            on_stop_clicked(nullptr);
        }
        else if (target == s_ui.set_button)
        {
            on_set_clicked(nullptr);
        }
        else if (target == s_ui.confirm_cancel)
        {
            on_confirm_cancel_clicked(nullptr);
        }
        else if (target == s_ui.confirm_apply)
        {
            on_confirm_apply_clicked(nullptr);
        }
        else
        {
            for (int index = 0; index < s_state.observation_count; ++index)
            {
                if (target == s_ui.result_rows[index])
                {
                    select_observation(s_state.visible_page_start + index);
                    open_confirmation();
                    return;
                }
            }
        }
        return;
    }
    handle_key_common(key);
}

void build_topbar(lv_obj_t* root)
{
    ::ui::widgets::TopBarConfig config{};
    config.height = s_layout.topbar_h;
    ::ui::widgets::top_bar_init(s_ui.top_bar, root, config);
    ::ui::widgets::top_bar_set_title(s_ui.top_bar, ::ui::i18n::tr("PACKET PROBE"));
    ::ui::widgets::top_bar_set_back_callback(s_ui.top_bar, top_bar_back_requested, nullptr);
    if (s_ui.top_bar.container)
    {
        lv_obj_set_pos(s_ui.top_bar.container, 0, 0);
    }
    if (s_ui.top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_ui.top_bar.back_btn, control_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    ui_update_top_bar_battery(s_ui.top_bar);
}

lv_obj_t* create_text(lv_obj_t* parent,
                      const char* text,
                      const lv_font_t* font,
                      uint32_t color,
                      lv_coord_t x,
                      lv_coord_t y)
{
    lv_obj_t* label = lv_label_create(parent);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

void build_work_area(lv_obj_t* root)
{
    const lv_coord_t work_height = s_layout.work_bottom - s_layout.work_top;
    s_ui.content_area = lv_obj_create(root);
    lv_obj_set_pos(s_ui.content_area, s_layout.content_x, s_layout.work_top);
    lv_obj_set_size(s_ui.content_area, s_layout.content_w, work_height);
    lv_obj_set_style_bg_opa(s_ui.content_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.content_area, 0, 0);
    lv_obj_set_style_radius(s_ui.content_area, 0, 0);
    lv_obj_set_style_pad_all(s_ui.content_area, 0, 0);
    lv_obj_clear_flag(s_ui.content_area, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.observed_label = create_text(s_ui.content_area,
                                      "OBSERVED PARAMETERS",
                                      &lv_font_montserrat_14,
                                      kColorText,
                                      0,
                                      s_layout.pager ? 7 : 12);
    s_ui.state_label = create_text(s_ui.content_area,
                                   "READY  1 SF LANE",
                                   &lv_font_montserrat_12,
                                   kColorTextDim,
                                   0,
                                   s_layout.pager ? 25 : 34);

    const lv_coord_t row_y = s_layout.pager ? 43 : 62;
    const lv_coord_t row_h = s_layout.pager ? 24 : 40;
    const lv_coord_t row_gap = s_layout.pager ? 3 : 8;
    for (int index = 0; index < kMaxObservations; ++index)
    {
        lv_obj_t* row = lv_btn_create(s_ui.content_area);
        lv_obj_set_pos(row, 0, row_y + static_cast<lv_coord_t>(index) * (row_h + row_gap));
        lv_obj_set_size(row, s_layout.content_w, row_h);
        lv_obj_set_style_bg_color(row, lv_color_hex(kColorPanelBg), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_outline_width(row, 0, LV_STATE_FOCUSED);
        lv_obj_add_event_cb(row,
                            on_result_row_clicked,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(index)));
        lv_obj_add_event_cb(row, control_key_event_cb, LV_EVENT_KEY, nullptr);
        s_ui.result_rows[index] = row;

        s_ui.result_primary[index] = create_text(row,
                                                 "---.--- MHz",
                                                 &lv_font_montserrat_14,
                                                 kColorText,
                                                 6,
                                                 s_layout.pager ? 1 : 5);
        s_ui.result_secondary[index] = create_text(row,
                                                   "BW ---  SF--  CR--",
                                                   &lv_font_montserrat_12,
                                                   kColorTextDim,
                                                   s_layout.pager ? 118 : 160,
                                                   s_layout.pager ? 4 : 11);
        s_ui.result_count[index] = create_text(row,
                                               "x0",
                                               &lv_font_montserrat_12,
                                               kColorOk,
                                               s_layout.content_w - (s_layout.pager ? 31 : 45),
                                               s_layout.pager ? 4 : 11);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    s_ui.empty_label = create_text(s_ui.content_area,
                                   "NO OBSERVED PROFILES",
                                   &lv_font_montserrat_12,
                                   kColorTextDim,
                                   0,
                                   row_y + 7);
    s_ui.progress_label = create_text(s_ui.content_area,
                                      "0/70 PROFILES  0 OBSERVED",
                                      &lv_font_montserrat_12,
                                      kColorTextDim,
                                      0,
                                      work_height - (s_layout.pager ? 18 : 26));
}

lv_obj_t* create_bottom_control(lv_obj_t* parent,
                                lv_coord_t x,
                                lv_coord_t width,
                                const char* text,
                                lv_event_cb_t callback,
                                lv_obj_t** out_label)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, 2);
    lv_obj_set_size(button, width, s_layout.bottom_bar_h - 4);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUSED);
    if (callback)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_add_event_cb(button, control_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* label = lv_label_create(button);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kColorText), 0);
    lv_obj_center(label);
    if (out_label)
    {
        *out_label = label;
    }
    return button;
}

void build_bottom_bar(lv_obj_t* root)
{
    s_ui.bottom_bar = lv_obj_create(root);
    lv_obj_set_pos(s_ui.bottom_bar, 0, s_layout.work_bottom);
    lv_obj_set_size(s_ui.bottom_bar, s_layout.screen_w, s_layout.bottom_bar_h);
    lv_obj_set_style_bg_color(s_ui.bottom_bar, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.bottom_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_ui.bottom_bar, 1, 0);
    lv_obj_set_style_border_color(s_ui.bottom_bar, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(s_ui.bottom_bar, 0, 0);
    lv_obj_clear_flag(s_ui.bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t gap = s_layout.pager ? 4 : 8;
    const lv_coord_t available = s_layout.screen_w - (gap * 5);
    const lv_coord_t first_width = (available * 30) / 100;
    const lv_coord_t action_width = (available - first_width) / 3;
    lv_coord_t x = gap;
    (void)create_bottom_control(s_ui.bottom_bar, x, first_width, "UP/DN  SELECT", nullptr, nullptr);
    x += first_width + gap;
    s_ui.set_button = create_bottom_control(s_ui.bottom_bar,
                                            x,
                                            action_width,
                                            "ENTER  SET",
                                            on_set_clicked,
                                            &s_ui.set_label);
    x += action_width + gap;
    s_ui.stop_button = create_bottom_control(s_ui.bottom_bar,
                                             x,
                                             action_width,
                                             "S  START",
                                             on_stop_clicked,
                                             &s_ui.stop_label);
    x += action_width + gap;
    (void)create_bottom_control(s_ui.bottom_bar,
                                x,
                                action_width,
                                "ESC  BACK",
                                on_back_requested,
                                nullptr);
}

void reset_ui_state()
{
    s_ui = {};
    s_layout = {};
}

} // namespace

lv_obj_t* ui_energy_sweep_create(lv_obj_t* parent)
{
    if (!parent)
    {
        return nullptr;
    }
    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_state();
    }

    s_layout = resolve_layout(parent);
    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, s_layout.screen_w, s_layout.screen_h);
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_radius(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    build_topbar(s_ui.root);
    build_work_area(s_ui.root);
    build_bottom_bar(s_ui.root);
    refresh_all_ui();
    return s_ui.root;
}

void ui_energy_sweep_enter(lv_obj_t* parent)
{
    lv_group_t* previous_group = lv_group_get_default();
    set_default_group(nullptr);

    s_state = {};
    setup_radio_context();
    ui_energy_sweep_create(parent);
    restore_page_focus();
    if (!::app_g)
    {
        set_default_group(previous_group);
    }

    platform::ui::screen::disable_sleep();
    if (!s_refresh_timer)
    {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, kRefreshIntervalMs, nullptr);
    }
    refresh_all_ui();
}

void ui_energy_sweep_exit(lv_obj_t* parent)
{
    (void)parent;
    if (s_refresh_timer)
    {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
    close_confirmation();
    stop_probe();
    platform::ui::screen::enable_sleep();

    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_state();
    }
    s_state = {};
    s_radio = {};
    s_band = {};
    s_host = nullptr;
}

namespace energy_sweep::ui::runtime
{

bool is_available()
{
    return platform::ui::lora::is_supported();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    s_host = host;
    ui_energy_sweep_enter(parent);
}

void exit(lv_obj_t* parent)
{
    ui_energy_sweep_exit(parent);
}

} // namespace energy_sweep::ui::runtime

#else

namespace energy_sweep::ui::runtime
{

bool is_available()
{
    return false;
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    (void)host;
    (void)parent;
}

void exit(lv_obj_t* parent)
{
    (void)parent;
}

} // namespace energy_sweep::ui::runtime

#endif
