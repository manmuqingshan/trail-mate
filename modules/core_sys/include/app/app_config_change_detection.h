#pragma once

#include "app/app_config.h"
#include "app/app_config_changes.h"

#include <cstddef>
#include <cstring>

namespace app
{
namespace detail
{
inline bool text_differs(const char* lhs, const char* rhs, std::size_t capacity)
{
    return std::strncmp(lhs ? lhs : "", rhs ? rhs : "", capacity) != 0;
}

inline bool bytes_differ(const void* lhs, const void* rhs, std::size_t len)
{
    return len > 0 && std::memcmp(lhs, rhs, len) != 0;
}

inline bool meshcore_channel_differs(const chat::MeshCoreChannelConfig& lhs,
                                     const chat::MeshCoreChannelConfig& rhs)
{
    return lhs.enabled != rhs.enabled ||
           text_differs(lhs.name, rhs.name, sizeof(lhs.name)) ||
           bytes_differ(lhs.key, rhs.key, sizeof(lhs.key));
}

inline bool meshtastic_profile_differs(const chat::MeshConfig& lhs,
                                       const chat::MeshConfig& rhs)
{
    return lhs.region != rhs.region ||
           lhs.modem_preset != rhs.modem_preset ||
           lhs.tx_power != rhs.tx_power ||
           lhs.hop_limit != rhs.hop_limit ||
           lhs.enable_relay != rhs.enable_relay ||
           lhs.use_preset != rhs.use_preset ||
           lhs.bandwidth_khz != rhs.bandwidth_khz ||
           lhs.spread_factor != rhs.spread_factor ||
           lhs.coding_rate != rhs.coding_rate ||
           lhs.tx_enabled != rhs.tx_enabled ||
           lhs.override_duty_cycle != rhs.override_duty_cycle ||
           lhs.channel_num != rhs.channel_num ||
           lhs.frequency_offset_mhz != rhs.frequency_offset_mhz ||
           lhs.override_frequency_mhz != rhs.override_frequency_mhz ||
           lhs.ignore_mqtt != rhs.ignore_mqtt ||
           lhs.config_ok_to_mqtt != rhs.config_ok_to_mqtt ||
           text_differs(lhs.primary_channel_name,
                        rhs.primary_channel_name,
                        sizeof(lhs.primary_channel_name)) ||
           text_differs(lhs.secondary_channel_name,
                        rhs.secondary_channel_name,
                        sizeof(lhs.secondary_channel_name)) ||
           lhs.primary_channel_id != rhs.primary_channel_id ||
           lhs.secondary_channel_id != rhs.secondary_channel_id ||
           lhs.primary_key_len != rhs.primary_key_len ||
           lhs.secondary_key_len != rhs.secondary_key_len ||
           bytes_differ(lhs.primary_key, rhs.primary_key, sizeof(lhs.primary_key)) ||
           bytes_differ(lhs.secondary_key, rhs.secondary_key, sizeof(lhs.secondary_key));
}

inline bool meshcore_profile_differs(const chat::MeshConfig& lhs,
                                     const chat::MeshConfig& rhs)
{
    if (lhs.meshcore_region_preset != rhs.meshcore_region_preset ||
        lhs.meshcore_freq_mhz != rhs.meshcore_freq_mhz ||
        lhs.meshcore_bw_khz != rhs.meshcore_bw_khz ||
        lhs.meshcore_sf != rhs.meshcore_sf ||
        lhs.meshcore_cr != rhs.meshcore_cr ||
        lhs.tx_power != rhs.tx_power ||
        lhs.meshcore_client_repeat != rhs.meshcore_client_repeat ||
        lhs.meshcore_rx_delay_base != rhs.meshcore_rx_delay_base ||
        lhs.meshcore_airtime_factor != rhs.meshcore_airtime_factor ||
        lhs.meshcore_flood_max != rhs.meshcore_flood_max ||
        lhs.meshcore_multi_acks != rhs.meshcore_multi_acks ||
        lhs.meshcore_send_profile != rhs.meshcore_send_profile ||
        lhs.meshcore_forward_profile != rhs.meshcore_forward_profile ||
        chat::normalizeMeshCoreChannelSlot(lhs.meshcore_channel_slot) !=
            chat::normalizeMeshCoreChannelSlot(rhs.meshcore_channel_slot) ||
        lhs.tx_enabled != rhs.tx_enabled ||
        lhs.meshcore_mqtt_enabled != rhs.meshcore_mqtt_enabled ||
        lhs.meshcore_mqtt_uplink_enabled != rhs.meshcore_mqtt_uplink_enabled ||
        lhs.meshcore_mqtt_downlink_enabled != rhs.meshcore_mqtt_downlink_enabled ||
        text_differs(lhs.meshcore_mqtt_host,
                     rhs.meshcore_mqtt_host,
                     sizeof(lhs.meshcore_mqtt_host)) ||
        lhs.meshcore_mqtt_port != rhs.meshcore_mqtt_port ||
        text_differs(lhs.meshcore_mqtt_root,
                     rhs.meshcore_mqtt_root,
                     sizeof(lhs.meshcore_mqtt_root)) ||
        text_differs(lhs.meshcore_mqtt_username,
                     rhs.meshcore_mqtt_username,
                     sizeof(lhs.meshcore_mqtt_username)) ||
        text_differs(lhs.meshcore_mqtt_password,
                     rhs.meshcore_mqtt_password,
                     sizeof(lhs.meshcore_mqtt_password)))
    {
        return true;
    }

    for (std::size_t index = 0; index < chat::kMeshCoreChannelMaxCount; ++index)
    {
        if (meshcore_channel_differs(lhs.meshcore_channels[index],
                                     rhs.meshcore_channels[index]))
        {
            return true;
        }
    }
    return false;
}

inline bool reticulum_profile_differs(const chat::MeshConfig& lhs,
                                      const chat::MeshConfig& rhs)
{
    return lhs.override_frequency_mhz != rhs.override_frequency_mhz ||
           lhs.bandwidth_khz != rhs.bandwidth_khz ||
           lhs.spread_factor != rhs.spread_factor ||
           lhs.coding_rate != rhs.coding_rate ||
           lhs.tx_power != rhs.tx_power ||
           lhs.tx_enabled != rhs.tx_enabled ||
           lhs.reticulum_lora_enabled != rhs.reticulum_lora_enabled ||
           lhs.reticulum_wifi_gateway_enabled != rhs.reticulum_wifi_gateway_enabled ||
           lhs.reticulum_wifi_auto_connect != rhs.reticulum_wifi_auto_connect ||
           lhs.reticulum_anonymous_peer != rhs.reticulum_anonymous_peer ||
           text_differs(lhs.reticulum_wifi_gateway_host,
                        rhs.reticulum_wifi_gateway_host,
                        sizeof(lhs.reticulum_wifi_gateway_host)) ||
           lhs.reticulum_wifi_gateway_port != rhs.reticulum_wifi_gateway_port ||
           lhs.reticulum_interface_policy != rhs.reticulum_interface_policy ||
           lhs.reticulum_allow_location_requests !=
               rhs.reticulum_allow_location_requests;
}

inline bool identity_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return text_differs(lhs.node_name, rhs.node_name, sizeof(lhs.node_name)) ||
           text_differs(lhs.short_name, rhs.short_name, sizeof(lhs.short_name));
}

inline bool mesh_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return chat::infra::normalizeMeshProtocol(lhs.mesh_protocol) !=
               chat::infra::normalizeMeshProtocol(rhs.mesh_protocol) ||
           lhs.chat_policy.enable_relay != rhs.chat_policy.enable_relay ||
           lhs.chat_policy.hop_limit_default != rhs.chat_policy.hop_limit_default ||
           lhs.chat_policy.ack_for_broadcast != rhs.chat_policy.ack_for_broadcast ||
           lhs.chat_policy.ack_for_squad != rhs.chat_policy.ack_for_squad ||
           lhs.chat_policy.max_tx_retries != rhs.chat_policy.max_tx_retries ||
           meshtastic_profile_differs(lhs.meshtastic_config,
                                      rhs.meshtastic_config) ||
           meshcore_profile_differs(lhs.meshcore_config, rhs.meshcore_config) ||
           reticulum_profile_differs(lhs.rnode_config, rhs.rnode_config) ||
           lhs.meshtastic_mqtt_enabled != rhs.meshtastic_mqtt_enabled ||
           lhs.meshtastic_mqtt_uplink_enabled != rhs.meshtastic_mqtt_uplink_enabled ||
           lhs.meshtastic_mqtt_downlink_enabled !=
               rhs.meshtastic_mqtt_downlink_enabled ||
           text_differs(lhs.meshtastic_mqtt_host,
                        rhs.meshtastic_mqtt_host,
                        sizeof(lhs.meshtastic_mqtt_host)) ||
           lhs.meshtastic_mqtt_port != rhs.meshtastic_mqtt_port ||
           text_differs(lhs.meshtastic_mqtt_root,
                        rhs.meshtastic_mqtt_root,
                        sizeof(lhs.meshtastic_mqtt_root)) ||
           text_differs(lhs.meshtastic_mqtt_username,
                        rhs.meshtastic_mqtt_username,
                        sizeof(lhs.meshtastic_mqtt_username)) ||
           text_differs(lhs.meshtastic_mqtt_password,
                        rhs.meshtastic_mqtt_password,
                        sizeof(lhs.meshtastic_mqtt_password));
}

inline bool channel_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return lhs.chat_policy.max_channels != rhs.chat_policy.max_channels ||
           lhs.primary_enabled != rhs.primary_enabled ||
           lhs.secondary_enabled != rhs.secondary_enabled ||
           lhs.primary_uplink_enabled != rhs.primary_uplink_enabled ||
           lhs.primary_downlink_enabled != rhs.primary_downlink_enabled ||
           lhs.secondary_uplink_enabled != rhs.secondary_uplink_enabled ||
           lhs.secondary_downlink_enabled != rhs.secondary_downlink_enabled ||
           lhs.primary_channel_has_module_settings !=
               rhs.primary_channel_has_module_settings ||
           lhs.primary_channel_position_precision !=
               rhs.primary_channel_position_precision ||
           lhs.primary_channel_is_muted != rhs.primary_channel_is_muted ||
           lhs.secondary_channel_has_module_settings !=
               rhs.secondary_channel_has_module_settings ||
           lhs.secondary_channel_position_precision !=
               rhs.secondary_channel_position_precision ||
           lhs.secondary_channel_is_muted != rhs.secondary_channel_is_muted;
}

inline bool gps_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return lhs.gps_enabled != rhs.gps_enabled ||
           lhs.gps_init_baud != rhs.gps_init_baud ||
           lhs.gps_init_probe_ms != rhs.gps_init_probe_ms ||
           lhs.gps_init_profile != rhs.gps_init_profile ||
           lhs.gps_init_rxm_policy != rhs.gps_init_rxm_policy ||
           lhs.gps_init_gnss_policy != rhs.gps_init_gnss_policy ||
           lhs.gps_init_nmea_policy != rhs.gps_init_nmea_policy ||
           lhs.gps_interval_ms != rhs.gps_interval_ms ||
           lhs.gps_mode != rhs.gps_mode ||
           lhs.gps_sat_mask != rhs.gps_sat_mask ||
           lhs.gps_strategy != rhs.gps_strategy ||
           lhs.gps_alt_ref != rhs.gps_alt_ref ||
           lhs.gps_coord_format != rhs.gps_coord_format ||
           lhs.motion_config.idle_timeout_ms !=
               rhs.motion_config.idle_timeout_ms ||
           lhs.motion_config.sensor_id != rhs.motion_config.sensor_id ||
           lhs.external_nmea_output_hz != rhs.external_nmea_output_hz ||
           lhs.external_nmea_sentence_mask != rhs.external_nmea_sentence_mask;
}

inline bool map_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return lhs.map_coord_system != rhs.map_coord_system ||
           lhs.map_source != rhs.map_source ||
           lhs.map_contour_enabled != rhs.map_contour_enabled ||
           lhs.map_track_enabled != rhs.map_track_enabled ||
           lhs.map_track_interval != rhs.map_track_interval ||
           lhs.map_track_format != rhs.map_track_format;
}

inline bool chat_ui_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return lhs.chat_channel != rhs.chat_channel;
}

inline bool network_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return lhs.net_duty_cycle != rhs.net_duty_cycle ||
           lhs.net_channel_util != rhs.net_channel_util;
}

inline bool privacy_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return lhs.privacy_encrypt_mode != rhs.privacy_encrypt_mode;
}

inline bool route_config_differs(const AppConfig& lhs, const AppConfig& rhs)
{
    return lhs.route_enabled != rhs.route_enabled ||
           text_differs(lhs.route_path, rhs.route_path, sizeof(lhs.route_path));
}

inline bool aprs_config_differs(const AprsConfig& lhs, const AprsConfig& rhs)
{
    return lhs.enabled != rhs.enabled ||
           text_differs(lhs.igate_callsign,
                        rhs.igate_callsign,
                        sizeof(lhs.igate_callsign)) ||
           lhs.igate_ssid != rhs.igate_ssid ||
           text_differs(lhs.tocall, rhs.tocall, sizeof(lhs.tocall)) ||
           text_differs(lhs.path, rhs.path, sizeof(lhs.path)) ||
           lhs.tx_min_interval_s != rhs.tx_min_interval_s ||
           lhs.dedupe_window_s != rhs.dedupe_window_s ||
           lhs.symbol_table != rhs.symbol_table ||
           lhs.symbol_code != rhs.symbol_code ||
           lhs.position_interval_s != rhs.position_interval_s ||
           lhs.node_map_len != rhs.node_map_len ||
           bytes_differ(lhs.node_map, rhs.node_map, lhs.node_map_len) ||
           lhs.self_enable != rhs.self_enable ||
           text_differs(lhs.self_callsign,
                        rhs.self_callsign,
                        sizeof(lhs.self_callsign));
}
} // namespace detail

inline AppConfigChangeSet detectAppConfigChanges(const AppConfig& before,
                                                 const AppConfig& after)
{
    AppConfigChangeSet changes;
    if (detail::identity_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::Identity);
    }
    if (detail::mesh_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::Mesh);
    }
    if (detail::channel_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::Channels);
    }
    if (detail::gps_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::Gps);
    }
    if (detail::map_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::Map);
    }
    if (detail::chat_ui_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::ChatUi);
    }
    if (detail::network_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::Network);
    }
    if (detail::privacy_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::Privacy);
    }
    if (detail::route_config_differs(before, after))
    {
        changes.add(AppConfigChangeDomain::Route);
    }
    if (detail::aprs_config_differs(before.aprs, after.aprs))
    {
        changes.add(AppConfigChangeDomain::Aprs);
    }
    return changes;
}

} // namespace app
