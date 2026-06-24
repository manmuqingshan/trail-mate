#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/ui/device_runtime.h"
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
#include <string>
#include <vector>

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_14
#endif

using Host = energy_sweep::ui::shell::Host;

namespace
{
const Host* s_host = nullptr;

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

constexpr int kScreenW = 480;
constexpr int kScreenH = 222;
constexpr int kTopBarH = 28;

constexpr int kLeftPanelX = 12;
constexpr int kLeftPanelY = 40;
constexpr int kLeftPanelW = 332;
constexpr int kLeftPanelH = 170;

constexpr int kPlotX = 10;
constexpr int kPlotY = 34;
constexpr int kPlotW = 312;
constexpr int kPlotH = 90;

constexpr int kScaleBarX = 10;
constexpr int kScaleBarY = 130;
constexpr int kScaleBarW = 312;
constexpr int kScaleBarH = 28;

constexpr int kRightPanelX = 354;
constexpr int kRightPanelY = 40;
constexpr int kRightPanelW = 114;
constexpr int kRightPanelH = 170;

constexpr float kDefaultFreqStartMhz = 433.050f;
constexpr float kDefaultFreqEndMhz = 434.790f;
constexpr float kStepQuantMhz = 0.025f;
constexpr int kTargetBinCount = 70;
constexpr int kMaxBins = 96;
constexpr int kScanIntervalMs = 35;
constexpr int kSampleSettleMs = 2;
constexpr int kSampleCount = 5;
constexpr int kSampleGapMs = 1;

constexpr float kNoiseEmaPrev = 0.7f;
constexpr float kNoiseEmaNew = 0.3f;
constexpr float kSweepEmaNew = 0.6f;
constexpr float kSweepEmaPrev = 0.4f;
constexpr float kHotEnterMarginDb = 10.0f;
constexpr float kHotExitMarginDb = 7.0f;
constexpr float kViewFloorHardMinDbm = -140.0f;
constexpr float kViewFloorHardMaxDbm = -100.0f;
constexpr float kViewCeilHardMinDbm = -90.0f;
constexpr float kViewCeilHardMaxDbm = -25.0f;
constexpr float kViewFloorMarginDb = 10.0f;
constexpr float kViewPeakMarginDb = 8.0f;
constexpr float kViewNoiseHeadroomDb = 18.0f;
constexpr float kViewMinSpanDb = 32.0f;
constexpr float kViewEmaPrev = 0.75f;
constexpr float kViewEmaNew = 0.25f;

constexpr int kBestGuardBins = 2;

constexpr uint32_t kColorAmber = 0xEBA341;
constexpr uint32_t kColorAmberDark = 0xC98118;
constexpr uint32_t kColorWarmBg = 0xF6E6C6;
constexpr uint32_t kColorPanelBg = 0xFAF0D8;
constexpr uint32_t kColorLine = 0xE7C98F;
constexpr uint32_t kColorText = 0x6B4A1E;
constexpr uint32_t kColorTextDim = 0x8A6A3A;
constexpr uint32_t kColorWarn = 0xB94A2C;
constexpr uint32_t kColorOk = 0x3E7D3E;
constexpr uint32_t kColorInfo = 0x2D6FB6;

struct EnergySweepUi
{
    lv_obj_t* root = nullptr;

    ::ui::widgets::TopBar top_bar = {};
    lv_obj_t* mode_chip = nullptr;
    lv_obj_t* mode_chip_label = nullptr;
    lv_obj_t* cad_chip = nullptr;
    lv_obj_t* cad_chip_label = nullptr;

    lv_obj_t* left_panel = nullptr;
    lv_obj_t* plot_area = nullptr;
    std::array<lv_obj_t*, kMaxBins> bars{};
    lv_obj_t* cursor_line = nullptr;
    lv_obj_t* cursor_tip = nullptr;
    lv_obj_t* scale_left = nullptr;
    lv_obj_t* scale_mid = nullptr;
    lv_obj_t* scale_right = nullptr;

    lv_obj_t* right_panel = nullptr;
    lv_obj_t* cursor_freq = nullptr;
    lv_obj_t* cursor_unit = nullptr;
    lv_obj_t* rssi_label = nullptr;
    lv_obj_t* noise_label = nullptr;
    lv_obj_t* best_freq = nullptr;
    lv_obj_t* best_snr = nullptr;
    lv_obj_t* progress_bar = nullptr;
    lv_obj_t* progress_pct = nullptr;
    lv_obj_t* btn_scan = nullptr;
    lv_obj_t* btn_scan_label = nullptr;
    lv_obj_t* btn_auto = nullptr;
    lv_obj_t* btn_auto_label = nullptr;
};

struct EnergySweepLayout
{
    bool large_touch = false;
    lv_coord_t screen_w = kScreenW;
    lv_coord_t screen_h = kScreenH;
    lv_coord_t topbar_h = kTopBarH;

    lv_coord_t mode_chip_x = 10;
    lv_coord_t mode_chip_y = 8;
    lv_coord_t mode_chip_w = 118;
    lv_coord_t mode_chip_h = 18;
    lv_coord_t cad_chip_x = 136;
    lv_coord_t cad_chip_y = 8;
    lv_coord_t cad_chip_w = 82;
    lv_coord_t cad_chip_h = 18;

    lv_coord_t left_panel_x = kLeftPanelX;
    lv_coord_t left_panel_y = kLeftPanelY;
    lv_coord_t left_panel_w = kLeftPanelW;
    lv_coord_t left_panel_h = kLeftPanelH;
    lv_coord_t plot_x = kPlotX;
    lv_coord_t plot_y = kPlotY;
    lv_coord_t plot_w = kPlotW;
    lv_coord_t plot_h = kPlotH;
    lv_coord_t scale_x = kScaleBarX;
    lv_coord_t scale_y = kScaleBarY;
    lv_coord_t scale_w = kScaleBarW;
    lv_coord_t scale_h = kScaleBarH;

    lv_coord_t right_panel_x = kRightPanelX;
    lv_coord_t right_panel_y = kRightPanelY;
    lv_coord_t right_panel_w = kRightPanelW;
    lv_coord_t right_panel_h = kRightPanelH;
};

struct RadioContext
{
    bool use_hw = false;
    float bw_khz = 125.0f;
    uint8_t sf = 11;
    uint8_t cr = 5;
    int8_t tx_power = 14;
    uint8_t preamble_len = 8;
    uint8_t sync_word = 0x12;
    uint8_t crc_len = 2;
};

struct SweepState
{
    bool scanning = false;
    bool auto_applied = false;
    int cursor_index = 0;
    int scan_index = 0;
    int scanned_bins = 0;
    int completed_cycles = 0;
    float progress = 0.0f;
    int best_index = 0;
    float noise_dbm = -104.0f;
    bool noise_valid = false;
    float view_floor_dbm = -130.0f;
    float view_ceil_dbm = -60.0f;
    bool view_valid = false;
    std::array<float, kMaxBins> rssi{};
    std::array<float, kMaxBins> smooth{};
    std::array<uint8_t, kMaxBins> hot{};
    uint32_t rand_state = 0xA5C34D29u;
    float sim_phase = 0.0f;
};

struct SweepBandPlan
{
    float freq_start_mhz = kDefaultFreqStartMhz;
    float freq_end_mhz = kDefaultFreqEndMhz;
    float step_mhz = kStepQuantMhz;
    float bw_khz = 125.0f;
    int bin_count = static_cast<int>(((kDefaultFreqEndMhz - kDefaultFreqStartMhz) / kStepQuantMhz) + 0.5f) + 1;
};

EnergySweepUi s_ui;
SweepState s_state;
RadioContext s_radio;
SweepBandPlan s_band;
EnergySweepLayout s_layout;
lv_timer_t* s_refresh_timer = nullptr;

EnergySweepLayout make_classic_layout()
{
    return {};
}

EnergySweepLayout make_large_touch_layout(lv_coord_t parent_w, lv_coord_t parent_h)
{
    EnergySweepLayout layout{};
    layout.large_touch = true;
    layout.screen_w = parent_w > 0 ? parent_w : 1168;
    layout.screen_h = parent_h > 0 ? parent_h : 540;
    layout.topbar_h = std::max<lv_coord_t>(48, ::ui::page_profile::current().top_bar_height);

    const bool landscape = layout.screen_w >= layout.screen_h;
    const lv_coord_t margin = 14;
    const lv_coord_t gap = 14;
    const lv_coord_t content_top = layout.topbar_h + 14;
    const lv_coord_t content_h = std::max<lv_coord_t>(360, layout.screen_h - content_top - margin);

    layout.mode_chip_w = 148;
    layout.mode_chip_h = 28;
    layout.mode_chip_x = 14;
    layout.mode_chip_y = 14;
    layout.cad_chip_w = 88;
    layout.cad_chip_h = 28;
    layout.cad_chip_x = layout.mode_chip_x + layout.mode_chip_w + 10;
    layout.cad_chip_y = layout.mode_chip_y;

    layout.left_panel_x = margin;
    layout.left_panel_y = content_top;
    if (landscape)
    {
        layout.right_panel_w = std::min<lv_coord_t>(340, std::max<lv_coord_t>(290, layout.screen_w / 4));
        layout.right_panel_x = layout.screen_w - margin - layout.right_panel_w;
        layout.right_panel_y = content_top;
        layout.right_panel_h = content_h;
        layout.left_panel_w = layout.right_panel_x - layout.left_panel_x - gap;
        layout.left_panel_h = content_h;
    }
    else
    {
        layout.left_panel_w = layout.screen_w - (margin * 2);
        layout.left_panel_h = std::max<lv_coord_t>(420, (content_h * 58) / 100);
        layout.right_panel_x = margin;
        layout.right_panel_y = layout.left_panel_y + layout.left_panel_h + gap;
        layout.right_panel_w = layout.left_panel_w;
        layout.right_panel_h = std::max<lv_coord_t>(340, layout.screen_h - layout.right_panel_y - margin);
    }

    layout.plot_x = 14;
    layout.plot_y = layout.mode_chip_y + layout.mode_chip_h + 14;
    layout.plot_w = layout.left_panel_w - 28;
    layout.scale_x = layout.plot_x;
    layout.scale_w = layout.plot_w;
    layout.scale_h = 50;
    layout.scale_y = layout.left_panel_h - layout.scale_h - 14;
    layout.plot_h = std::max<lv_coord_t>(180, layout.scale_y - layout.plot_y - 12);
    return layout;
}

EnergySweepLayout resolve_layout(lv_obj_t* parent)
{
    if (parent)
    {
        lv_obj_update_layout(parent);
    }
    const lv_coord_t parent_w = parent ? lv_obj_get_width(parent) : 0;
    const lv_coord_t parent_h = parent ? lv_obj_get_height(parent) : 0;
    const lv_coord_t long_side = std::max(parent_w, parent_h);
    const lv_coord_t short_side = std::min(parent_w, parent_h);
    if (::ui::page_profile::current().large_touch_hitbox && long_side >= 900 && short_side >= 500)
    {
        return make_large_touch_layout(parent_w, parent_h);
    }
    return make_classic_layout();
}

platform::ui::lora::ReceiveConfig make_receive_config()
{
    platform::ui::lora::ReceiveConfig config{};
    config.bw_khz = s_radio.bw_khz;
    config.sf = s_radio.sf;
    config.cr = s_radio.cr;
    config.tx_power = s_radio.tx_power;
    config.preamble_len = s_radio.preamble_len;
    config.sync_word = s_radio.sync_word;
    config.crc_len = s_radio.crc_len;
    return config;
}

int active_bin_count()
{
    if (s_band.bin_count < 2)
    {
        return 2;
    }
    if (s_band.bin_count > kMaxBins)
    {
        return kMaxBins;
    }
    return s_band.bin_count;
}

int clamp_index(int idx)
{
    if (idx < 0)
    {
        return 0;
    }
    const int bins = active_bin_count();
    if (idx >= bins)
    {
        return bins - 1;
    }
    return idx;
}

float clamp_float(float value, float low, float high)
{
    if (value < low)
    {
        return low;
    }
    if (value > high)
    {
        return high;
    }
    return value;
}

float bin_to_freq_mhz(int idx)
{
    return s_band.freq_start_mhz + static_cast<float>(idx) * s_band.step_mhz;
}

float preset_to_bw_khz(uint8_t modem_preset, bool wide_lora)
{
    const auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(modem_preset);
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        return wide_lora ? 1625.0f : 500.0f;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        return wide_lora ? 812.5f : 250.0f;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        return wide_lora ? 406.25f : 125.0f;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
        return wide_lora ? 1625.0f : 500.0f;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
    default:
        return wide_lora ? 812.5f : 250.0f;
    }
}

const chat::meshtastic::RegionInfo* find_region_for_frequency(float freq_mhz)
{
    size_t count = 0;
    const chat::meshtastic::RegionInfo* regions = chat::meshtastic::getRegionTable(&count);
    if (!regions || count == 0)
    {
        return nullptr;
    }

    const chat::meshtastic::RegionInfo* best = nullptr;
    float best_dist = 1e9f;

    for (size_t i = 0; i < count; ++i)
    {
        const chat::meshtastic::RegionInfo& region = regions[i];
        if (region.code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            continue;
        }

        float dist = 0.0f;
        if (freq_mhz < region.freq_start_mhz)
        {
            dist = region.freq_start_mhz - freq_mhz;
        }
        else if (freq_mhz > region.freq_end_mhz)
        {
            dist = freq_mhz - region.freq_end_mhz;
        }

        if (!best || dist < best_dist)
        {
            best = &region;
            best_dist = dist;
        }
    }

    return best ? best : chat::meshtastic::findRegion(meshtastic_Config_LoRaConfig_RegionCode_CN);
}

void apply_band_plan(float start_mhz, float end_mhz, float bw_khz)
{
    if (!std::isfinite(start_mhz) || !std::isfinite(end_mhz))
    {
        start_mhz = kDefaultFreqStartMhz;
        end_mhz = kDefaultFreqEndMhz;
    }
    if (end_mhz < start_mhz)
    {
        std::swap(start_mhz, end_mhz);
    }

    const float safe_bw_khz = (std::isfinite(bw_khz) && bw_khz > 1.0f) ? bw_khz : 125.0f;
    const float half_bw_mhz = safe_bw_khz / 2000.0f;
    if ((end_mhz - start_mhz) > (2.0f * half_bw_mhz))
    {
        start_mhz += half_bw_mhz;
        end_mhz -= half_bw_mhz;
    }

    const float span_mhz = std::max(kStepQuantMhz, end_mhz - start_mhz);
    float step_mhz = span_mhz / static_cast<float>(kTargetBinCount - 1);
    if (!std::isfinite(step_mhz) || step_mhz < kStepQuantMhz)
    {
        step_mhz = kStepQuantMhz;
    }
    step_mhz = std::ceil(step_mhz / kStepQuantMhz) * kStepQuantMhz;

    int bins = static_cast<int>(std::floor(span_mhz / step_mhz)) + 1;
    if (bins < 2)
    {
        bins = 2;
    }
    while (bins > kMaxBins)
    {
        step_mhz += kStepQuantMhz;
        bins = static_cast<int>(std::floor(span_mhz / step_mhz)) + 1;
    }

    s_band.freq_start_mhz = start_mhz;
    s_band.step_mhz = step_mhz;
    s_band.bin_count = bins;
    s_band.freq_end_mhz = start_mhz + step_mhz * static_cast<float>(bins - 1);
    s_band.bw_khz = safe_bw_khz;
}

void setup_sweep_band_plan()
{
    const app::AppConfig& cfg = app::configFacade().getConfig();

    float start_mhz = kDefaultFreqStartMhz;
    float end_mhz = kDefaultFreqEndMhz;
    float bw_khz = 125.0f;

    if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
    {
        const chat::MeshConfig& mesh = cfg.meshtastic_config;
        auto region_code = static_cast<meshtastic_Config_LoRaConfig_RegionCode>(mesh.region);
        if (region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
        }
        const chat::meshtastic::RegionInfo* region = chat::meshtastic::findRegion(region_code);
        if (region)
        {
            start_mhz = region->freq_start_mhz;
            end_mhz = region->freq_end_mhz;
            bw_khz = mesh.use_preset ? preset_to_bw_khz(mesh.modem_preset, region->wide_lora)
                                     : mesh.bandwidth_khz;
        }
    }
    else
    {
        const chat::MeshConfig& mesh = cfg.meshcore_config;
        float hint_freq_mhz = mesh.meshcore_freq_mhz;
        if (mesh.meshcore_region_preset > 0)
        {
            const chat::meshcore::RegionPreset* preset =
                chat::meshcore::findRegionPresetById(mesh.meshcore_region_preset);
            if (preset)
            {
                hint_freq_mhz = preset->freq_mhz;
            }
        }

        const chat::meshtastic::RegionInfo* region = find_region_for_frequency(hint_freq_mhz);
        if (region)
        {
            start_mhz = region->freq_start_mhz;
            end_mhz = region->freq_end_mhz;
        }
        bw_khz = mesh.meshcore_bw_khz;
    }

    apply_band_plan(start_mhz, end_mhz, bw_khz);
}

uint32_t next_random()
{
    uint32_t x = s_state.rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_state.rand_state = x;
    return x;
}

float random_unit()
{
    return static_cast<float>(next_random() & 0xFFFFu) / 65535.0f;
}

float simulated_rssi_for_bin(int idx)
{
    const float t = s_state.sim_phase;
    float value = -111.0f + 3.2f * std::sin((idx + t) * 0.21f) + 2.4f * std::cos((idx + t) * 0.067f);
    value += (random_unit() - 0.5f) * 3.5f;

    if (idx >= 32 && idx <= 38)
    {
        value = -92.0f + (random_unit() - 0.5f) * 3.0f;
    }

    const int bins = active_bin_count();
    const int moving_peak = (bins > 0) ? (static_cast<int>(t) % bins) : 0;
    const int dist = std::abs(idx - moving_peak);
    if (dist <= 2)
    {
        value = std::max(value, -89.0f - static_cast<float>(dist) * 1.2f + (random_unit() - 0.5f) * 1.4f);
    }

    return std::max(-124.0f, std::min(-82.0f, value));
}

float sample_hw_rssi(int idx)
{
    if (!s_radio.use_hw)
    {
        return NAN;
    }

    const float freq_mhz = bin_to_freq_mhz(idx);
    if (!platform::ui::lora::configure_receive(freq_mhz, make_receive_config()))
    {
        return NAN;
    }
    platform::ui::device::delay_ms(kSampleSettleMs);

    std::array<float, kSampleCount> values{};
    int valid = 0;
    for (int i = 0; i < kSampleCount; ++i)
    {
        const float rssi = platform::ui::lora::read_instant_rssi();
        if (std::isfinite(rssi) && rssi < 0.0f && rssi > -180.0f)
        {
            values[valid++] = rssi;
        }
        platform::ui::device::delay_ms(kSampleGapMs);
    }

    if (valid <= 0)
    {
        return NAN;
    }

    for (int i = 1; i < valid; ++i)
    {
        const float current = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > current)
        {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = current;
    }
    return values[valid / 2];
}

float sample_bin_rssi(int idx)
{
    const float hw = sample_hw_rssi(idx);
    if (std::isfinite(hw))
    {
        return hw;
    }
    return simulated_rssi_for_bin(idx);
}

float display_value_for_bin(int idx)
{
    const int bins = active_bin_count();
    if (idx < 0 || idx >= bins)
    {
        return -120.0f;
    }
    const float smooth = s_state.smooth[idx];
    if (smooth < -190.0f)
    {
        return s_state.rssi[idx];
    }
    return smooth;
}

int available_bins_for_metrics()
{
    const int bins = active_bin_count();
    if (s_state.completed_cycles > 0)
    {
        return bins;
    }
    if (s_state.scanned_bins > 0)
    {
        return std::min(s_state.scanned_bins, bins);
    }
    return 1;
}

void recompute_noise_and_hot(int available)
{
    const int bins = active_bin_count();
    available = std::max(1, std::min(available, bins));

    std::vector<float> values;
    values.reserve(static_cast<size_t>(available));
    for (int i = 0; i < available; ++i)
    {
        values.push_back(display_value_for_bin(i));
    }

    const size_t p20 = (values.size() - 1) / 5;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(p20), values.end());
    const float floor_est = values[p20];
    if (!s_state.noise_valid)
    {
        s_state.noise_dbm = floor_est;
        s_state.noise_valid = true;
    }
    else
    {
        s_state.noise_dbm = (kNoiseEmaPrev * s_state.noise_dbm) + (kNoiseEmaNew * floor_est);
    }

    const float hot_enter = s_state.noise_dbm + kHotEnterMarginDb;
    const float hot_exit = s_state.noise_dbm + kHotExitMarginDb;
    for (int i = 0; i < kMaxBins; ++i)
    {
        if (i >= available)
        {
            s_state.hot[i] = 0;
            continue;
        }

        const float v = display_value_for_bin(i);
        if (s_state.hot[i])
        {
            s_state.hot[i] = (v > hot_exit) ? 1 : 0;
        }
        else
        {
            s_state.hot[i] = (v > hot_enter) ? 1 : 0;
        }
    }
}

void recompute_view_range(int available)
{
    const int bins = active_bin_count();
    available = std::max(1, std::min(available, bins));

    float observed_min = 0.0f;
    float observed_max = 0.0f;
    bool have_observation = false;
    for (int i = 0; i < available; ++i)
    {
        const float value = display_value_for_bin(i);
        if (!std::isfinite(value))
        {
            continue;
        }
        if (!have_observation)
        {
            observed_min = value;
            observed_max = value;
            have_observation = true;
            continue;
        }
        observed_min = std::min(observed_min, value);
        observed_max = std::max(observed_max, value);
    }

    if (!have_observation)
    {
        return;
    }

    float target_floor = std::min(observed_min - 4.0f, s_state.noise_dbm - kViewFloorMarginDb);
    target_floor = clamp_float(target_floor, kViewFloorHardMinDbm, kViewFloorHardMaxDbm);

    float target_ceil = std::max(observed_max + kViewPeakMarginDb, s_state.noise_dbm + kViewNoiseHeadroomDb);
    target_ceil = clamp_float(target_ceil, kViewCeilHardMinDbm, kViewCeilHardMaxDbm);

    if ((target_ceil - target_floor) < kViewMinSpanDb)
    {
        target_ceil = std::min(kViewCeilHardMaxDbm, target_floor + kViewMinSpanDb);
    }
    if ((target_ceil - target_floor) < kViewMinSpanDb)
    {
        target_floor = std::max(kViewFloorHardMinDbm, target_ceil - kViewMinSpanDb);
    }

    if (!s_state.view_valid)
    {
        s_state.view_floor_dbm = target_floor;
        s_state.view_ceil_dbm = target_ceil;
        s_state.view_valid = true;
        return;
    }

    s_state.view_floor_dbm = (kViewEmaPrev * s_state.view_floor_dbm) + (kViewEmaNew * target_floor);
    s_state.view_ceil_dbm = (kViewEmaPrev * s_state.view_ceil_dbm) + (kViewEmaNew * target_ceil);
}

void recompute_best(int available)
{
    const int bins = active_bin_count();
    available = std::max(1, std::min(available, bins));

    const float step_khz = std::max(1.0f, s_band.step_mhz * 1000.0f);
    const int window_bins = std::max(1, static_cast<int>(std::ceil(s_band.bw_khz / step_khz)));
    const int half_span = ((window_bins - 1) / 2) + kBestGuardBins;

    int best_idx = 0;
    float best_score = 1e9f;
    for (int i = 0; i < available; ++i)
    {
        const int lo = std::max(0, i - half_span);
        const int hi = std::min(available - 1, i + half_span);
        float window_worst = -200.0f;
        for (int j = lo; j <= hi; ++j)
        {
            window_worst = std::max(window_worst, display_value_for_bin(j));
        }
        if (window_worst < best_score)
        {
            best_score = window_worst;
            best_idx = i;
        }
    }

    s_state.best_index = clamp_index(best_idx);
}

void process_scan_step()
{
    if (!s_state.scanning)
    {
        return;
    }

    const int bins = active_bin_count();
    const int idx = clamp_index(s_state.scan_index);
    const float sample = sample_bin_rssi(idx);

    s_state.rssi[idx] = sample;
    const float prev = s_state.smooth[idx];
    if (prev < -190.0f)
    {
        s_state.smooth[idx] = sample;
    }
    else
    {
        s_state.smooth[idx] = (kSweepEmaNew * sample) + (kSweepEmaPrev * prev);
    }

    s_state.cursor_index = idx;
    s_state.scan_index++;
    s_state.scanned_bins = std::max(s_state.scanned_bins, s_state.scan_index);
    s_state.progress = static_cast<float>(s_state.scan_index) / static_cast<float>(bins);

    if (s_state.scan_index >= bins)
    {
        s_state.progress = 1.0f;
        s_state.scan_index = 0;
        s_state.scanned_bins = bins;
        s_state.completed_cycles++;
    }

    const int available = available_bins_for_metrics();
    recompute_noise_and_hot(available);
    recompute_view_range(available);
    recompute_best(available);
    s_state.sim_phase += 0.17f;
}

void set_scan_button_style()
{
    if (!s_ui.btn_scan || !s_ui.btn_scan_label)
    {
        return;
    }

    const uint32_t bg = s_state.scanning ? kColorWarn : kColorAmber;
    const uint32_t border = s_state.scanning ? 0x8A2E1C : kColorAmberDark;
    lv_obj_set_style_bg_color(s_ui.btn_scan, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(s_ui.btn_scan, lv_color_hex(border), 0);
    lv_obj_set_style_text_color(s_ui.btn_scan_label, lv_color_white(), 0);
    ::ui::i18n::set_label_text(s_ui.btn_scan_label, s_state.scanning ? "STOP" : "SCAN");
    lv_obj_center(s_ui.btn_scan_label);
}

void set_auto_button_style()
{
    if (!s_ui.btn_auto || !s_ui.btn_auto_label)
    {
        return;
    }

    if (s_state.auto_applied)
    {
        lv_obj_set_style_bg_color(s_ui.btn_auto, lv_color_hex(kColorInfo), 0);
        lv_obj_set_style_bg_opa(s_ui.btn_auto, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_ui.btn_auto, lv_color_hex(0x1F4E84), 0);
        lv_obj_set_style_text_color(s_ui.btn_auto_label, lv_color_white(), 0);
    }
    else
    {
        lv_obj_set_style_bg_color(s_ui.btn_auto, lv_color_hex(kColorPanelBg), 0);
        lv_obj_set_style_bg_opa(s_ui.btn_auto, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_ui.btn_auto, lv_color_hex(kColorInfo), 0);
        lv_obj_set_style_text_color(s_ui.btn_auto_label, lv_color_hex(kColorInfo), 0);
    }
}

void refresh_page_status()
{
    if (!s_ui.mode_chip || !s_ui.mode_chip_label || !s_ui.cad_chip || !s_ui.cad_chip_label)
    {
        return;
    }

    lv_obj_set_style_bg_color(s_ui.mode_chip,
                              lv_color_hex(s_state.scanning ? kColorAmber : 0xD4BE8E),
                              0);
    lv_obj_set_style_border_color(s_ui.mode_chip, lv_color_hex(kColorAmberDark), 0);
    lv_obj_set_style_text_color(s_ui.mode_chip_label, lv_color_hex(kColorText), 0);
    ::ui::i18n::set_label_text(s_ui.mode_chip_label, "MODE: RSSI");

    if (s_radio.use_hw)
    {
        const bool blink = s_state.scanning && ((sys::millis_now() / 450u) % 2u == 0u);
        lv_obj_set_style_bg_color(s_ui.cad_chip, lv_color_hex(blink ? kColorInfo : 0x245B95), 0);
        lv_obj_set_style_border_color(s_ui.cad_chip, lv_color_hex(0x1C4B7F), 0);
        lv_obj_set_style_text_color(s_ui.cad_chip_label, lv_color_white(), 0);
        ::ui::i18n::set_label_text(s_ui.cad_chip_label, "CAD");
    }
    else if (s_state.scanning)
    {
        lv_obj_set_style_bg_color(s_ui.cad_chip, lv_color_hex(0xD3C8AE), 0);
        lv_obj_set_style_border_color(s_ui.cad_chip, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_text_color(s_ui.cad_chip_label, lv_color_hex(kColorTextDim), 0);
        ::ui::i18n::set_label_text(s_ui.cad_chip_label, "SIM");
    }
    else
    {
        lv_obj_set_style_bg_color(s_ui.cad_chip, lv_color_hex(0xD6E3D2), 0);
        lv_obj_set_style_border_color(s_ui.cad_chip, lv_color_hex(0x7E9A76), 0);
        lv_obj_set_style_text_color(s_ui.cad_chip_label, lv_color_hex(kColorOk), 0);
        ::ui::i18n::set_label_text(s_ui.cad_chip_label, "MESH");
    }
}

void refresh_plot()
{
    if (!s_ui.plot_area)
    {
        return;
    }

    const int bins = active_bin_count();
    for (int i = 0; i < kMaxBins; ++i)
    {
        lv_obj_t* bar = s_ui.bars[i];
        if (!bar)
        {
            continue;
        }

        if (i >= bins)
        {
            lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);

        const float v = display_value_for_bin(i);
        const float view_floor = s_state.view_valid ? s_state.view_floor_dbm : -130.0f;
        const float view_ceil = s_state.view_valid ? s_state.view_ceil_dbm : -60.0f;
        const float view_span = std::max(1.0f, view_ceil - view_floor);
        float t = (v - view_floor) / view_span;
        if (t < 0.0f)
        {
            t = 0.0f;
        }
        else if (t > 1.0f)
        {
            t = 1.0f;
        }

        const int x0 = (i * s_layout.plot_w) / bins;
        const int x1 = ((i + 1) * s_layout.plot_w) / bins;
        int w = x1 - x0 - 1;
        if (w < 2)
        {
            w = 2;
        }
        if (x0 + w > s_layout.plot_w)
        {
            w = s_layout.plot_w - x0;
        }
        if (w <= 0)
        {
            w = 1;
        }

        int h = static_cast<int>(std::round(t * static_cast<float>(s_layout.plot_h)));
        if (h < 2)
        {
            h = 2;
        }
        if (h > s_layout.plot_h)
        {
            h = s_layout.plot_h;
        }

        lv_obj_set_pos(bar, x0, s_layout.plot_h - h);
        lv_obj_set_size(bar, w, h);
        lv_obj_set_style_bg_color(bar,
                                  lv_color_hex(s_state.hot[i] ? kColorWarn : kColorAmber),
                                  0);
    }

    const int idx = clamp_index(s_state.cursor_index);
    const int c0 = (idx * s_layout.plot_w) / bins;
    const int c1 = ((idx + 1) * s_layout.plot_w) / bins;
    const int cx = (c0 + c1) / 2;

    if (s_ui.cursor_line)
    {
        lv_obj_set_pos(s_ui.cursor_line, cx - 1, 0);
        lv_obj_set_size(s_ui.cursor_line, 2, s_layout.plot_h);
        lv_obj_move_foreground(s_ui.cursor_line);
    }
    if (s_ui.cursor_tip)
    {
        lv_obj_set_pos(s_ui.cursor_tip, cx - 6, s_layout.plot_h - 14);
        lv_obj_move_foreground(s_ui.cursor_tip);
    }
}

void refresh_right_panel_text()
{
    const int cursor = clamp_index(s_state.cursor_index);
    const int best = clamp_index(s_state.best_index);
    const float cursor_freq = bin_to_freq_mhz(cursor);
    const float cursor_rssi = display_value_for_bin(cursor);
    const float best_freq = bin_to_freq_mhz(best);
    const float best_rssi = display_value_for_bin(best);
    const int cleanliness = static_cast<int>(std::lround(std::max(0.0f, s_state.noise_dbm - best_rssi)));

    if (s_ui.cursor_freq)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.3f", cursor_freq);
        lv_label_set_text(s_ui.cursor_freq, buf);
    }

    if (s_ui.rssi_label)
    {
        char buf[32];
        const std::string text = ::ui::i18n::format("RSSI %.0f dBm", cursor_rssi);
        snprintf(buf, sizeof(buf), "%s", text.c_str());
        lv_label_set_text(s_ui.rssi_label, buf);
        lv_obj_set_style_text_color(
            s_ui.rssi_label,
            lv_color_hex(s_state.hot[cursor] ? kColorWarn : kColorText),
            0);
    }

    if (s_ui.noise_label)
    {
        char buf[32];
        const std::string text = ::ui::i18n::format("NOISE %.0f dBm", s_state.noise_dbm);
        snprintf(buf, sizeof(buf), "%s", text.c_str());
        lv_label_set_text(s_ui.noise_label, buf);
    }

    if (s_ui.best_freq)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.3f", best_freq);
        lv_label_set_text(s_ui.best_freq, buf);
    }

    if (s_ui.best_snr)
    {
        char buf[24];
        const std::string text = ::ui::i18n::format("SNR +%d", cleanliness);
        snprintf(buf, sizeof(buf), "%s", text.c_str());
        lv_label_set_text(s_ui.best_snr, buf);
    }

    if (s_ui.progress_bar)
    {
        int pct = static_cast<int>(std::lround(s_state.progress * 100.0f));
        if (pct < 0)
        {
            pct = 0;
        }
        if (pct > 100)
        {
            pct = 100;
        }
        lv_bar_set_value(s_ui.progress_bar, pct, LV_ANIM_OFF);

        if (s_ui.progress_pct)
        {
            char buf[12];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            lv_label_set_text(s_ui.progress_pct, buf);
        }
    }
}

void refresh_scale_labels()
{
    if (!s_ui.scale_left || !s_ui.scale_mid || !s_ui.scale_right)
    {
        return;
    }

    const int bins = active_bin_count();
    const float end_freq = bin_to_freq_mhz(bins - 1);

    char left[20];
    char right[20];
    char mid[40];

    snprintf(left, sizeof(left), "%.3f", s_band.freq_start_mhz);
    snprintf(right, sizeof(right), "%.3f", end_freq);

    const float step_khz = s_band.step_mhz * 1000.0f;
    const float step_round = std::round(step_khz);
    const float bw_round = std::round(s_band.bw_khz);
    const bool step_int = std::fabs(step_khz - step_round) < 0.05f;
    const bool bw_int = std::fabs(s_band.bw_khz - bw_round) < 0.05f;

    if (step_int && bw_int)
    {
        const std::string text =
            ::ui::i18n::format("STEP %.0fk | BW %.0fk",
                               static_cast<double>(step_khz),
                               static_cast<double>(s_band.bw_khz));
        snprintf(mid, sizeof(mid), "%s", text.c_str());
    }
    else
    {
        const std::string text =
            ::ui::i18n::format("STEP %.1fk | BW %.1fk",
                               static_cast<double>(step_khz),
                               static_cast<double>(s_band.bw_khz));
        snprintf(mid, sizeof(mid), "%s", text.c_str());
    }

    lv_label_set_text(s_ui.scale_left, left);
    lv_label_set_text(s_ui.scale_mid, mid);
    lv_label_set_text(s_ui.scale_right, right);
}

void refresh_all_ui()
{
    refresh_page_status();
    ui_update_top_bar_battery(s_ui.top_bar);
    refresh_scale_labels();
    refresh_plot();
    refresh_right_panel_text();
    set_scan_button_style();
    set_auto_button_style();
}

void on_back_requested(lv_event_t*)
{
    request_exit();
}

void top_bar_back_requested(void*)
{
    on_back_requested(nullptr);
}

void apply_auto_choice()
{
    s_state.auto_applied = true;
    s_state.cursor_index = clamp_index(s_state.best_index);

    if (s_radio.use_hw)
    {
        const float best_freq = bin_to_freq_mhz(s_state.best_index);
        platform::ui::lora::configure_receive(best_freq, make_receive_config());
    }
}

void reset_scan_progress()
{
    s_state.scan_index = 0;
    s_state.scanned_bins = 0;
    s_state.completed_cycles = 0;
    s_state.progress = 0.0f;
}

bool acquire_radio_runtime(float freq_mhz)
{
    if (s_radio.use_hw)
    {
        return platform::ui::lora::configure_receive(freq_mhz, make_receive_config());
    }

    if (!platform::ui::lora::acquire() || !platform::ui::lora::is_online())
    {
        s_radio.use_hw = false;
        return false;
    }

    s_radio.use_hw = true;
    if (platform::ui::lora::configure_receive(freq_mhz, make_receive_config()))
    {
        return true;
    }

    platform::ui::lora::release();
    s_radio.use_hw = false;
    return false;
}

void release_radio_runtime()
{
    if (!s_radio.use_hw)
    {
        return;
    }

    platform::ui::lora::release();
    s_radio.use_hw = false;
}

void on_scan_btn_clicked(lv_event_t*)
{
    s_state.auto_applied = false;
    if (s_state.scanning)
    {
        s_state.scanning = false;
        release_radio_runtime();
    }
    else
    {
        reset_scan_progress();
        (void)acquire_radio_runtime(s_band.freq_start_mhz);
        s_state.scanning = true;
    }
    refresh_all_ui();
}

void on_auto_btn_clicked(lv_event_t*)
{
    apply_auto_choice();
    refresh_all_ui();
}

void move_cursor_manual(int delta)
{
    if (s_state.scanning)
    {
        return;
    }
    s_state.cursor_index = clamp_index(s_state.cursor_index + delta);
    refresh_all_ui();
}

void handle_key_common(uint32_t key)
{
    if (key == LV_KEY_BACKSPACE)
    {
        on_back_requested(nullptr);
        return;
    }
    if (key == LV_KEY_LEFT)
    {
        move_cursor_manual(-1);
        return;
    }
    if (key == LV_KEY_RIGHT)
    {
        move_cursor_manual(1);
        return;
    }
}

void root_key_event_cb(lv_event_t* e)
{
    handle_key_common(lv_event_get_key(e));
}

void back_btn_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER)
    {
        on_back_requested(nullptr);
        return;
    }
    handle_key_common(key);
}

void scan_btn_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER)
    {
        on_scan_btn_clicked(nullptr);
        return;
    }
    handle_key_common(key);
}

void auto_btn_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER)
    {
        on_auto_btn_clicked(nullptr);
        return;
    }
    handle_key_common(key);
}

void setup_radio_context()
{
    s_radio = {};
    setup_sweep_band_plan();

    const app::AppConfig& cfg = app::configFacade().getConfig();
    s_radio.bw_khz = s_band.bw_khz;

    if (cfg.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        const chat::MeshConfig& mesh = cfg.meshcore_config;
        s_radio.sf = (mesh.meshcore_sf >= 5 && mesh.meshcore_sf <= 12) ? mesh.meshcore_sf : 9;
        s_radio.cr = (mesh.meshcore_cr >= 5 && mesh.meshcore_cr <= 8) ? mesh.meshcore_cr : 5;
        s_radio.tx_power = mesh.tx_power;
    }
    else
    {
        const chat::MeshConfig& mesh = cfg.meshtastic_config;
        s_radio.sf = (mesh.spread_factor >= 5 && mesh.spread_factor <= 12) ? mesh.spread_factor : 11;
        s_radio.cr = (mesh.coding_rate >= 5 && mesh.coding_rate <= 8) ? mesh.coding_rate : 5;
        s_radio.tx_power = mesh.tx_power;
    }
}

void teardown_radio_context()
{
    release_radio_runtime();
    s_radio = {};
}

void init_sweep_state()
{
    s_state = {};
    const int bins = active_bin_count();
    s_state.scanning = false;
    s_state.noise_dbm = -104.0f;
    s_state.noise_valid = true;
    s_state.view_floor_dbm = -130.0f;
    s_state.view_ceil_dbm = -60.0f;
    s_state.view_valid = true;
    s_state.rand_state ^= static_cast<uint32_t>(sys::millis_now());
    s_state.sim_phase = random_unit() * 37.0f;
    s_state.cursor_index = bins / 2;

    for (int i = 0; i < kMaxBins; ++i)
    {
        if (i < bins)
        {
            const float v = simulated_rssi_for_bin(i);
            s_state.rssi[i] = v;
            s_state.smooth[i] = v;
        }
        else
        {
            s_state.rssi[i] = -200.0f;
            s_state.smooth[i] = -200.0f;
        }
        s_state.hot[i] = 0;
    }
    s_state.scanned_bins = bins;
    s_state.completed_cycles = 1;
    s_state.scan_index = 0;
    s_state.progress = 0.0f;
    recompute_noise_and_hot(bins);
    recompute_view_range(bins);
    recompute_best(bins);
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

void build_topbar(lv_obj_t* root)
{
    ::ui::widgets::TopBarConfig config{};
    config.height = s_layout.topbar_h;
    ::ui::widgets::top_bar_init(s_ui.top_bar, root, config);
    ::ui::widgets::top_bar_set_title(s_ui.top_bar, ::ui::i18n::tr("SUB-GHz SCAN"));
    ::ui::widgets::top_bar_set_back_callback(s_ui.top_bar, top_bar_back_requested, nullptr);
    if (s_ui.top_bar.container)
    {
        lv_obj_set_pos(s_ui.top_bar.container, 0, 0);
    }
    if (s_ui.top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_ui.top_bar.back_btn, back_btn_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    ui_update_top_bar_battery(s_ui.top_bar);
}

void build_left_panel(lv_obj_t* root)
{
    s_ui.left_panel = lv_obj_create(root);
    lv_obj_set_pos(s_ui.left_panel, s_layout.left_panel_x, s_layout.left_panel_y);
    lv_obj_set_size(s_ui.left_panel, s_layout.left_panel_w, s_layout.left_panel_h);
    lv_obj_set_style_bg_color(s_ui.left_panel, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.left_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.left_panel, 2, 0);
    lv_obj_set_style_border_color(s_ui.left_panel, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.left_panel, 0, 0);
    lv_obj_set_style_pad_all(s_ui.left_panel, 0, 0);
    lv_obj_clear_flag(s_ui.left_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.mode_chip = lv_obj_create(s_ui.left_panel);
    lv_obj_set_pos(s_ui.mode_chip, s_layout.mode_chip_x, s_layout.mode_chip_y);
    lv_obj_set_size(s_ui.mode_chip, s_layout.mode_chip_w, s_layout.mode_chip_h);
    lv_obj_set_style_bg_color(s_ui.mode_chip, lv_color_hex(kColorAmber), 0);
    lv_obj_set_style_bg_opa(s_ui.mode_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.mode_chip, 1, 0);
    lv_obj_set_style_border_color(s_ui.mode_chip, lv_color_hex(kColorAmberDark), 0);
    lv_obj_set_style_radius(s_ui.mode_chip, 4, 0);
    lv_obj_set_style_pad_all(s_ui.mode_chip, 0, 0);
    lv_obj_clear_flag(s_ui.mode_chip, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.mode_chip_label = lv_label_create(s_ui.mode_chip);
    ::ui::i18n::set_label_text(s_ui.mode_chip_label, "MODE: RSSI");
    lv_obj_set_style_text_font(s_ui.mode_chip_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.mode_chip_label, lv_color_hex(kColorText), 0);
    lv_obj_center(s_ui.mode_chip_label);

    s_ui.cad_chip = lv_obj_create(s_ui.left_panel);
    lv_obj_set_pos(s_ui.cad_chip, s_layout.cad_chip_x, s_layout.cad_chip_y);
    lv_obj_set_size(s_ui.cad_chip, s_layout.cad_chip_w, s_layout.cad_chip_h);
    lv_obj_set_style_bg_color(s_ui.cad_chip, lv_color_hex(kColorInfo), 0);
    lv_obj_set_style_bg_opa(s_ui.cad_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.cad_chip, 1, 0);
    lv_obj_set_style_border_color(s_ui.cad_chip, lv_color_hex(0x1C4B7F), 0);
    lv_obj_set_style_radius(s_ui.cad_chip, 4, 0);
    lv_obj_set_style_pad_all(s_ui.cad_chip, 0, 0);
    lv_obj_clear_flag(s_ui.cad_chip, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.cad_chip_label = lv_label_create(s_ui.cad_chip);
    ::ui::i18n::set_label_text(s_ui.cad_chip_label, "CAD");
    lv_obj_set_style_text_font(s_ui.cad_chip_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.cad_chip_label, lv_color_white(), 0);
    lv_obj_center(s_ui.cad_chip_label);

    s_ui.plot_area = lv_obj_create(s_ui.left_panel);
    lv_obj_set_pos(s_ui.plot_area, s_layout.plot_x, s_layout.plot_y);
    lv_obj_set_size(s_ui.plot_area, s_layout.plot_w, s_layout.plot_h);
    lv_obj_set_style_bg_color(s_ui.plot_area, lv_color_hex(0xF2E4C8), 0);
    lv_obj_set_style_bg_opa(s_ui.plot_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.plot_area, 1, 0);
    lv_obj_set_style_border_color(s_ui.plot_area, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.plot_area, 0, 0);
    lv_obj_set_style_pad_all(s_ui.plot_area, 0, 0);
    lv_obj_clear_flag(s_ui.plot_area, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 1; i <= 4; ++i)
    {
        lv_obj_t* grid = lv_obj_create(s_ui.plot_area);
        lv_obj_set_pos(grid, 0, (i * s_layout.plot_h) / 5);
        lv_obj_set_size(grid, s_layout.plot_w, 1);
        lv_obj_set_style_bg_color(grid, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_50, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_radius(grid, 0, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    }

    for (int i = 0; i < kMaxBins; ++i)
    {
        lv_obj_t* bar = lv_obj_create(s_ui.plot_area);
        lv_obj_set_style_bg_color(bar, lv_color_hex(kColorAmber), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        s_ui.bars[i] = bar;
    }

    s_ui.cursor_line = lv_obj_create(s_ui.plot_area);
    lv_obj_set_size(s_ui.cursor_line, 2, s_layout.plot_h);
    lv_obj_set_style_bg_color(s_ui.cursor_line, lv_color_hex(kColorInfo), 0);
    lv_obj_set_style_bg_opa(s_ui.cursor_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.cursor_line, 0, 0);
    lv_obj_set_style_radius(s_ui.cursor_line, 0, 0);
    lv_obj_clear_flag(s_ui.cursor_line, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.cursor_tip = lv_label_create(s_ui.plot_area);
    lv_label_set_text(s_ui.cursor_tip, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(s_ui.cursor_tip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.cursor_tip, lv_color_hex(kColorInfo), 0);

    lv_obj_t* scale_bar = lv_obj_create(s_ui.left_panel);
    lv_obj_set_pos(scale_bar, s_layout.scale_x, s_layout.scale_y);
    lv_obj_set_size(scale_bar, s_layout.scale_w, s_layout.scale_h);
    lv_obj_set_style_bg_opa(scale_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scale_bar, 1, 0);
    lv_obj_set_style_border_color(scale_bar, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_border_side(scale_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(scale_bar, 0, 0);
    lv_obj_set_style_radius(scale_bar, 0, 0);
    lv_obj_clear_flag(scale_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.scale_left = lv_label_create(scale_bar);
    lv_label_set_text(s_ui.scale_left, "----");
    lv_obj_set_style_text_font(s_ui.scale_left, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.scale_left, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(s_ui.scale_left, 2, 6);

    s_ui.scale_mid = lv_label_create(scale_bar);
    ::ui::i18n::set_label_text(s_ui.scale_mid, "STEP -- | BW --");
    lv_obj_set_style_text_font(s_ui.scale_mid, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ui.scale_mid, lv_color_hex(kColorTextDim), 0);
    lv_obj_align(s_ui.scale_mid, LV_ALIGN_CENTER, 0, 5);

    s_ui.scale_right = lv_label_create(scale_bar);
    lv_label_set_text(s_ui.scale_right, "----");
    lv_obj_set_style_text_font(s_ui.scale_right, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.scale_right, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_align(s_ui.scale_right, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_ui.scale_right, 84);
    lv_obj_set_pos(s_ui.scale_right, s_layout.scale_w - 86, 6);
}

void build_right_panel(lv_obj_t* root)
{
    const bool large = s_layout.large_touch;
    const lv_coord_t pad = large ? 18 : 8;
    const lv_coord_t sep_y = large ? std::min<lv_coord_t>(150, s_layout.right_panel_h / 3) : 76;
    const lv_coord_t cursor_title_y = large ? 14 : 2;
    const lv_coord_t cursor_freq_y = large ? 40 : 14;
    const lv_coord_t unit_x = large ? std::min<lv_coord_t>(150, s_layout.right_panel_w - 54) : 84;
    const lv_coord_t unit_y = large ? 48 : 22;
    const lv_coord_t rssi_y = large ? 82 : 43;
    const lv_coord_t noise_y = large ? 112 : 60;
    const lv_coord_t best_title_y = large ? sep_y + 16 : 80;
    const lv_coord_t best_freq_y = large ? best_title_y + 30 : 97;
    const lv_coord_t best_snr_y = large ? best_freq_y + 32 : 114;
    const lv_coord_t progress_y = large ? best_snr_y + 42 : 120;
    const lv_coord_t progress_w = large ? std::max<lv_coord_t>(80, s_layout.right_panel_w - (pad * 2) - 54) : 66;
    const lv_coord_t progress_pct_x = large ? pad + progress_w + 8 : 78;
    const lv_coord_t btn_h = large ? 50 : 28;
    const lv_coord_t btn_gap = large ? 14 : 6;
    const lv_coord_t btn_y = large ? s_layout.right_panel_h - btn_h - 18 : 134;
    const lv_coord_t btn_w = large ? (s_layout.right_panel_w - (pad * 2) - btn_gap) / 2 : 46;

    s_ui.right_panel = lv_obj_create(root);
    lv_obj_set_pos(s_ui.right_panel, s_layout.right_panel_x, s_layout.right_panel_y);
    lv_obj_set_size(s_ui.right_panel, s_layout.right_panel_w, s_layout.right_panel_h);
    lv_obj_set_style_bg_color(s_ui.right_panel, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.right_panel, 2, 0);
    lv_obj_set_style_border_color(s_ui.right_panel, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.right_panel, 0, 0);
    lv_obj_set_style_pad_all(s_ui.right_panel, 0, 0);
    lv_obj_clear_flag(s_ui.right_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* sep1 = lv_obj_create(s_ui.right_panel);
    lv_obj_set_pos(sep1, 0, sep_y);
    lv_obj_set_size(sep1, s_layout.right_panel_w, 1);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_80, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);
    lv_obj_clear_flag(sep1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_cursor = lv_label_create(s_ui.right_panel);
    ::ui::i18n::set_label_text(title_cursor, "CURSOR");
    lv_obj_set_style_text_font(title_cursor, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_cursor, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(title_cursor, pad, cursor_title_y);

    s_ui.cursor_freq = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.cursor_freq, "433.550");
    lv_obj_set_style_text_font(s_ui.cursor_freq, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.cursor_freq, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(s_ui.cursor_freq, pad, cursor_freq_y);

    s_ui.cursor_unit = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.cursor_unit, "MHz");
    lv_obj_set_style_text_font(s_ui.cursor_unit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ui.cursor_unit, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(s_ui.cursor_unit, unit_x, unit_y);

    s_ui.rssi_label = lv_label_create(s_ui.right_panel);
    ::ui::i18n::set_label_text(s_ui.rssi_label, "RSSI -92 dBm");
    lv_obj_set_style_text_font(s_ui.rssi_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.rssi_label, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(s_ui.rssi_label, pad, rssi_y);

    s_ui.noise_label = lv_label_create(s_ui.right_panel);
    ::ui::i18n::set_label_text(s_ui.noise_label, "NOISE -104 dBm");
    lv_obj_set_style_text_font(s_ui.noise_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.noise_label, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(s_ui.noise_label, pad, noise_y);

    lv_obj_t* title_best = lv_label_create(s_ui.right_panel);
    ::ui::i18n::set_label_text(title_best, "BEST");
    lv_obj_set_style_text_font(title_best, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_best, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(title_best, pad, best_title_y);

    s_ui.best_freq = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.best_freq, "434.125");
    lv_obj_set_style_text_font(s_ui.best_freq, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.best_freq, lv_color_hex(kColorOk), 0);
    lv_obj_set_pos(s_ui.best_freq, pad, best_freq_y);

    s_ui.best_snr = lv_label_create(s_ui.right_panel);
    ::ui::i18n::set_label_text(s_ui.best_snr, "SNR +12");
    lv_obj_set_style_text_font(s_ui.best_snr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ui.best_snr, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(s_ui.best_snr, pad, best_snr_y);

    s_ui.progress_bar = lv_bar_create(s_ui.right_panel);
    lv_obj_set_pos(s_ui.progress_bar, pad, progress_y);
    lv_obj_set_size(s_ui.progress_bar, progress_w, large ? 16 : 12);
    lv_bar_set_range(s_ui.progress_bar, 0, 100);
    lv_bar_set_value(s_ui.progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.progress_bar, lv_color_hex(kColorPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.progress_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.progress_bar, lv_color_hex(kColorLine), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.progress_bar, lv_color_hex(kColorAmberDark), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.progress_bar, 0, LV_PART_INDICATOR);

    s_ui.progress_pct = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.progress_pct, "0%");
    lv_obj_set_style_text_font(s_ui.progress_pct, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ui.progress_pct, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(s_ui.progress_pct, progress_pct_x, progress_y - 2);

    s_ui.btn_scan = lv_btn_create(s_ui.right_panel);
    lv_obj_set_pos(s_ui.btn_scan, pad, btn_y);
    lv_obj_set_size(s_ui.btn_scan, btn_w, btn_h);
    lv_obj_set_style_bg_color(s_ui.btn_scan, lv_color_hex(kColorWarn), 0);
    lv_obj_set_style_bg_opa(s_ui.btn_scan, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.btn_scan, 1, 0);
    lv_obj_set_style_border_color(s_ui.btn_scan, lv_color_hex(0x8A2E1C), 0);
    lv_obj_set_style_radius(s_ui.btn_scan, 5, 0);
    lv_obj_set_style_outline_width(s_ui.btn_scan, 0, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_ui.btn_scan, on_scan_btn_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_ui.btn_scan, scan_btn_key_event_cb, LV_EVENT_KEY, nullptr);

    s_ui.btn_scan_label = lv_label_create(s_ui.btn_scan);
    ::ui::i18n::set_label_text(s_ui.btn_scan_label, "STOP");
    lv_obj_set_style_text_font(s_ui.btn_scan_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.btn_scan_label, lv_color_white(), 0);
    lv_obj_center(s_ui.btn_scan_label);

    s_ui.btn_auto = lv_btn_create(s_ui.right_panel);
    lv_obj_set_pos(s_ui.btn_auto, pad + btn_w + btn_gap, btn_y);
    lv_obj_set_size(s_ui.btn_auto, btn_w, btn_h);
    lv_obj_set_style_bg_color(s_ui.btn_auto, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.btn_auto, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.btn_auto, 1, 0);
    lv_obj_set_style_border_color(s_ui.btn_auto, lv_color_hex(kColorInfo), 0);
    lv_obj_set_style_radius(s_ui.btn_auto, 5, 0);
    lv_obj_set_style_outline_width(s_ui.btn_auto, 0, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_ui.btn_auto, on_auto_btn_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_ui.btn_auto, auto_btn_key_event_cb, LV_EVENT_KEY, nullptr);

    s_ui.btn_auto_label = lv_label_create(s_ui.btn_auto);
    ::ui::i18n::set_label_text(s_ui.btn_auto_label, "AUTO");
    lv_obj_set_style_text_font(s_ui.btn_auto_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.btn_auto_label, lv_color_hex(kColorInfo), 0);
    lv_obj_center(s_ui.btn_auto_label);
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
    build_left_panel(s_ui.root);
    build_right_panel(s_ui.root);
    refresh_all_ui();

    return s_ui.root;
}

void ui_energy_sweep_enter(lv_obj_t* parent)
{
    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    setup_radio_context();
    init_sweep_state();
    ui_energy_sweep_create(parent);

    lv_obj_t* back_btn = s_ui.top_bar.back_btn;
    if (::app_g && back_btn)
    {
        lv_group_remove_all_objs(::app_g);
        lv_group_add_obj(::app_g, back_btn);
        if (s_ui.btn_scan)
        {
            lv_group_add_obj(::app_g, s_ui.btn_scan);
        }
        if (s_ui.btn_auto)
        {
            lv_group_add_obj(::app_g, s_ui.btn_auto);
        }
        lv_group_focus_obj(back_btn);
        set_default_group(::app_g);
        lv_group_set_editing(::app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    platform::ui::screen::disable_sleep();

    if (!s_refresh_timer)
    {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, kScanIntervalMs, nullptr);
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

    teardown_radio_context();
    platform::ui::screen::enable_sleep();

    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_state();
    }

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
