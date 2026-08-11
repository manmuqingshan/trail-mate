#include "chat/infra/meshtastic/mt_radio_config.h"

#include <cassert>
#include <cmath>
#include <cstring>

namespace
{

bool sameFloat(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 0.0001f;
}

void assertSameRadioConfig(const chat::meshtastic::RadioConfig& lhs,
                           const chat::meshtastic::RadioConfig& rhs)
{
    assert(lhs.region_code == rhs.region_code);
    assert(lhs.modem_preset == rhs.modem_preset);
    assert(lhs.using_preset == rhs.using_preset);
    assert(lhs.channel_slot == rhs.channel_slot);
    assert(sameFloat(lhs.freq_mhz, rhs.freq_mhz));
    assert(sameFloat(lhs.bw_khz, rhs.bw_khz));
    assert(lhs.sf == rhs.sf);
    assert(lhs.cr_denom == rhs.cr_denom);
    assert(lhs.tx_power_dbm == rhs.tx_power_dbm);
    assert(lhs.preamble_len == rhs.preamble_len);
    assert(lhs.sync_word == rhs.sync_word);
    assert(lhs.crc_len == rhs.crc_len);
}

void assertForcedPresetMatchesRealConfig(chat::MeshConfig config,
                                         meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    const chat::meshtastic::RadioConfig candidate =
        chat::meshtastic::deriveRadioConfigForModemPreset(config, preset);

    config.use_preset = true;
    config.modem_preset = preset;
    const chat::meshtastic::RadioConfig expected =
        chat::meshtastic::deriveRadioConfig(config);

    assertSameRadioConfig(candidate, expected);
    assert(std::strcmp(candidate.channel_name, expected.channel_name) == 0);
}

} // namespace

int main()
{
    chat::MeshConfig config{};
    config.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.use_preset = false;
    config.bandwidth_khz = 125.0f;
    config.spread_factor = 10;
    config.coding_rate = 8;
    config.tx_power = 20;
    std::strcpy(config.primary_channel_name, "TrailMate Probe");

    assertForcedPresetMatchesRealConfig(
        config, meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW);
    assertForcedPresetMatchesRealConfig(
        config, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST);

    config.channel_num = 13;
    assertForcedPresetMatchesRealConfig(
        config, meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST);

    config.override_frequency_mhz = 910.25f;
    config.frequency_offset_mhz = 0.0125f;
    assertForcedPresetMatchesRealConfig(
        config, meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE);

    return 0;
}
