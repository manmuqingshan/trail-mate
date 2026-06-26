#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "fake_phone_runtime_context.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_session.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "meshtastic/admin.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

namespace
{

constexpr uint8_t kMeshCoreResponseError = 1;
constexpr uint8_t kMeshCoreErrorIllegalArg = 6;

class FakeMeshtasticTransport final : public phone::meshtastic::MeshtasticPhoneTransport
{
  public:
    bool isBleConnected() const override { return connected; }
    void notifyFromNum(uint32_t from_num) override
    {
        last_from_num = from_num;
        notify_count++;
    }

    bool connected = true;
    uint32_t last_from_num = 0;
    int notify_count = 0;
};

class FakeBluetoothConfigHooks final : public phone::meshtastic::MeshtasticPhoneBluetoothConfigHooks
{
  public:
    bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig*) const override { return false; }
    void saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config) override
    {
        last_saved = config;
        save_count++;
    }

    int save_count = 0;
    meshtastic_Config_BluetoothConfig last_saved = meshtastic_Config_BluetoothConfig_init_zero;
};

class FakeModuleConfigHooks final : public phone::meshtastic::MeshtasticPhoneModuleConfigHooks
{
  public:
    bool loadModuleConfig(meshtastic_LocalModuleConfig*) const override { return false; }
    void saveModuleConfig(const meshtastic_LocalModuleConfig& config) override
    {
        last_saved = config;
        save_count++;
    }

    int save_count = 0;
    meshtastic_LocalModuleConfig last_saved = meshtastic_LocalModuleConfig_init_zero;
};

void copyBounded(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    size_t i = 0;
    for (; src && src[i] != '\0' && i + 1 < dst_len; ++i)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

bool encodeAdminToRadio(const meshtastic_AdminMessage& admin,
                        uint8_t* out,
                        size_t out_len,
                        size_t& written,
                        uint32_t packet_id)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.id = packet_id;
    packet.to = 0x12345678;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded = meshtastic_Data_init_zero;
    packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    packet.decoded.dest = packet.to;
    packet.decoded.want_response = false;

    pb_ostream_t admin_stream = pb_ostream_from_buffer(packet.decoded.payload.bytes,
                                                       sizeof(packet.decoded.payload.bytes));
    if (!pb_encode(&admin_stream, meshtastic_AdminMessage_fields, &admin))
    {
        return false;
    }
    packet.decoded.payload.size = static_cast<pb_size_t>(admin_stream.bytes_written);

    meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
    to_radio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    to_radio.packet = packet;

    pb_ostream_t to_radio_stream = pb_ostream_from_buffer(out, out_len);
    if (!pb_encode(&to_radio_stream, meshtastic_ToRadio_fields, &to_radio))
    {
        return false;
    }
    written = to_radio_stream.bytes_written;
    return true;
}

bool encodeAdminSetChannelToRadio(uint8_t* out, size_t out_len, size_t& written, uint32_t packet_id)
{
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_set_channel_tag;
    admin.set_channel.index = 1;
    admin.set_channel.has_settings = true;
    admin.set_channel.role = meshtastic_Channel_Role_SECONDARY;
    admin.set_channel.settings.channel_num = 1;
    admin.set_channel.settings.id = 0xAABBCCDD;
    admin.set_channel.settings.psk.size = 32;
    for (uint8_t index = 0; index < admin.set_channel.settings.psk.size; ++index)
    {
        admin.set_channel.settings.psk.bytes[index] = static_cast<uint8_t>(index + 1);
    }
    admin.set_channel.settings.uplink_enabled = true;
    admin.set_channel.settings.downlink_enabled = true;
    copyBounded(admin.set_channel.settings.name, sizeof(admin.set_channel.settings.name), "vic");

    return encodeAdminToRadio(admin, out, out_len, written, packet_id);
}

bool encodeAdminSetPrimaryCustomChannelToRadio(uint8_t* out,
                                               size_t out_len,
                                               size_t& written,
                                               uint32_t packet_id)
{
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_set_channel_tag;
    admin.set_channel.index = 0;
    admin.set_channel.has_settings = true;
    admin.set_channel.role = meshtastic_Channel_Role_PRIMARY;
    admin.set_channel.settings.channel_num = 0;
    admin.set_channel.settings.id = 0x10203040;
    admin.set_channel.settings.psk.size = 1;
    admin.set_channel.settings.psk.bytes[0] = 1;
    admin.set_channel.settings.uplink_enabled = true;
    admin.set_channel.settings.downlink_enabled = true;
    admin.set_channel.settings.has_module_settings = true;
    admin.set_channel.settings.module_settings.position_precision = 32;
    admin.set_channel.settings.module_settings.is_muted = true;
    copyBounded(admin.set_channel.settings.name, sizeof(admin.set_channel.settings.name), "Custom");

    return encodeAdminToRadio(admin, out, out_len, written, packet_id);
}

bool encodeAdminSetManualLoraConfigToRadio(uint8_t* out,
                                           size_t out_len,
                                           size_t& written,
                                           uint32_t packet_id)
{
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    admin.set_config.which_payload_variant = meshtastic_Config_lora_tag;
    auto& lora = admin.set_config.payload_variant.lora;
    lora.use_preset = false;
    lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    lora.bandwidth = 62;
    lora.spread_factor = 11;
    lora.coding_rate = 8;
    lora.frequency_offset = 0.125f;
    lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    lora.hop_limit = 5;
    lora.tx_enabled = true;
    lora.tx_power = 20;
    lora.channel_num = 7;
    lora.override_duty_cycle = true;
    lora.override_frequency = 906.875f;
    lora.ignore_mqtt = true;
    lora.config_ok_to_mqtt = true;

    return encodeAdminToRadio(admin, out, out_len, written, packet_id);
}

bool encodeAdminSetBluetoothConfigToRadio(uint8_t* out,
                                          size_t out_len,
                                          size_t& written,
                                          uint32_t packet_id)
{
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    admin.set_config.which_payload_variant = meshtastic_Config_bluetooth_tag;
    auto& bluetooth = admin.set_config.payload_variant.bluetooth;
    bluetooth.enabled = false;
    bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    bluetooth.fixed_pin = 123456;

    return encodeAdminToRadio(admin, out, out_len, written, packet_id);
}

bool encodeAdminBeginEditSettingsToRadio(uint8_t* out, size_t out_len, size_t& written, uint32_t packet_id)
{
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_begin_edit_settings_tag;
    admin.begin_edit_settings = true;

    return encodeAdminToRadio(admin, out, out_len, written, packet_id);
}

bool encodeAdminCommitEditSettingsToRadio(uint8_t* out, size_t out_len, size_t& written, uint32_t packet_id)
{
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_commit_edit_settings_tag;
    admin.commit_edit_settings = true;

    return encodeAdminToRadio(admin, out, out_len, written, packet_id);
}

bool encodeAdminSetMqttModuleConfigToRadio(uint8_t* out,
                                           size_t out_len,
                                           size_t& written,
                                           uint32_t packet_id)
{
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_set_module_config_tag;
    admin.set_module_config.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
    auto& mqtt = admin.set_module_config.payload_variant.mqtt;
    mqtt.enabled = true;
    mqtt.proxy_to_client_enabled = false;
    mqtt.encryption_enabled = true;
    mqtt.tls_enabled = false;
    mqtt.map_reporting_enabled = true;
    mqtt.has_map_report_settings = true;
    mqtt.map_report_settings.publish_interval_secs = 3600;
    mqtt.map_report_settings.position_precision = 16;
    mqtt.map_report_settings.should_report_location = true;
    copyBounded(mqtt.address, sizeof(mqtt.address), "mqtt.example.test");
    copyBounded(mqtt.username, sizeof(mqtt.username), "trail");
    copyBounded(mqtt.password, sizeof(mqtt.password), "mate");
    copyBounded(mqtt.root, sizeof(mqtt.root), "msh");

    return encodeAdminToRadio(admin, out, out_len, written, packet_id);
}

bool encodeWantConfigToRadio(uint8_t* out, size_t out_len, size_t& written, uint32_t nonce)
{
    meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
    to_radio.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    to_radio.want_config_id = nonce;

    pb_ostream_t to_radio_stream = pb_ostream_from_buffer(out, out_len);
    if (!pb_encode(&to_radio_stream, meshtastic_ToRadio_fields, &to_radio))
    {
        return false;
    }
    written = to_radio_stream.bytes_written;
    return true;
}

bool decodeFromRadio(const phone::meshtastic::MeshtasticBleFrame& frame, meshtastic_FromRadio& out)
{
    out = meshtastic_FromRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(frame.buf, frame.len);
    return pb_decode(&stream, meshtastic_FromRadio_fields, &out);
}

} // namespace

int main()
{
    phone::tests::FakePhoneRuntimeContext runtime;
    FakeMeshtasticTransport transport;

    phone::meshtastic::MeshtasticPhoneSession meshtastic_session(
        runtime, transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    const uint8_t invalid_meshtastic[] = {0xFF, 0xFF, 0xFF};
    assert(!meshtastic_session.handleToRadio(invalid_meshtastic, sizeof(invalid_meshtastic)));
    meshtastic_session.close();

    phone::tests::FakePhoneRuntimeContext text_runtime;
    FakeMeshtasticTransport text_transport;
    phone::meshtastic::MeshtasticPhoneSession text_session(
        text_runtime, text_transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    chat::MeshIncomingText incoming_text{};
    incoming_text.channel = chat::ChannelId::PRIMARY;
    incoming_text.from = 0x4A59CD8C;
    incoming_text.to = 0xFFFFFFFF;
    incoming_text.msg_id = 0x9DD4E0E7;
    incoming_text.timestamp = 123456789;
    incoming_text.text = "Hi";
    incoming_text.hop_limit = 7;
    text_session.onIncomingText(incoming_text);

    phone::meshtastic::MeshtasticBleFrame text_frame{};
    assert(text_session.popToPhone(&text_frame));
    meshtastic_FromRadio text_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(text_frame, text_from));
    assert(text_frame.from_num == incoming_text.msg_id);
    assert(text_from.which_payload_variant == meshtastic_FromRadio_packet_tag);
    assert(text_from.packet.from == incoming_text.from);
    assert(text_from.packet.to == incoming_text.to);
    assert(!text_from.packet.via_mqtt);
    assert(text_from.packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP);
    assert(text_from.packet.decoded.source == 0);
    assert(text_from.packet.decoded.dest == 0);
    assert(!text_from.packet.decoded.has_bitfield);
    assert(text_from.packet.decoded.payload.size == incoming_text.text.size());
    assert(std::memcmp(text_from.packet.decoded.payload.bytes,
                       incoming_text.text.data(),
                       incoming_text.text.size()) == 0);
    assert(!text_session.popToPhone(&text_frame));

    incoming_text.msg_id = 0x9DD4E0E8;
    incoming_text.rx_meta.from_is = true;
    incoming_text.rx_meta.relay_node = 7;
    text_session.onIncomingText(incoming_text);
    assert(text_session.popToPhone(&text_frame));
    text_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(text_frame, text_from));
    assert(text_from.packet.id == incoming_text.msg_id);
    assert(text_from.packet.via_mqtt);
    assert(text_from.packet.relay_node == incoming_text.rx_meta.relay_node);
    assert(text_from.packet.decoded.source == 0);
    assert(text_from.packet.decoded.dest == 0);
    assert(!text_from.packet.decoded.has_bitfield);
    assert(!text_session.popToPhone(&text_frame));

    chat::MeshIncomingData incoming_data{};
    incoming_data.portnum = meshtastic_PortNum_POSITION_APP;
    incoming_data.from = incoming_text.from;
    incoming_data.to = incoming_text.to;
    incoming_data.packet_id = 0x9DD4E0E9;
    incoming_data.channel = chat::ChannelId::PRIMARY;
    incoming_data.hop_limit = 4;
    incoming_data.rx_meta.from_is = true;
    incoming_data.payload = {0x01, 0x02};
    text_session.onIncomingData(incoming_data);
    assert(text_session.popToPhone(&text_frame));
    text_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(text_frame, text_from));
    assert(text_from.packet.id == incoming_data.packet_id);
    assert(text_from.packet.via_mqtt);
    assert(text_from.packet.decoded.source == incoming_data.from);
    assert(text_from.packet.decoded.dest == incoming_data.to);
    assert(text_from.packet.decoded.has_bitfield);
    assert(!text_session.popToPhone(&text_frame));

    phone::tests::FakePhoneRuntimeContext admin_runtime;
    FakeMeshtasticTransport admin_transport;
    phone::meshtastic::MeshtasticPhoneSession admin_session(
        admin_runtime, admin_transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    constexpr uint32_t kSetChannelPacketId = 0xABCDEF12;
    uint8_t set_channel_to_radio[meshtastic_ToRadio_size] = {};
    size_t set_channel_to_radio_len = 0;
    assert(encodeAdminSetChannelToRadio(set_channel_to_radio,
                                        sizeof(set_channel_to_radio),
                                        set_channel_to_radio_len,
                                        kSetChannelPacketId));
    assert(admin_session.handleToRadio(set_channel_to_radio, set_channel_to_radio_len));
    assert(admin_runtime.save_config_count == 0);
    assert(admin_runtime.apply_mesh_config_count == 1);
    const auto saved_admin_config = admin_runtime.getMeshtasticPhoneConfig();
    assert(saved_admin_config.mesh.secondary_key_len == 32);
    assert(std::memcmp(saved_admin_config.mesh.secondary_key,
                       "\x01\x02\x03\x04\x05\x06\x07\x08"
                       "\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10"
                       "\x11\x12\x13\x14\x15\x16\x17\x18"
                       "\x19\x1A\x1B\x1C\x1D\x1E\x1F\x20",
                       32) == 0);

    phone::meshtastic::MeshtasticBleFrame first_frame{};
    assert(admin_session.popToPhone(&first_frame));
    meshtastic_FromRadio first_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(first_frame, first_from));
    assert(first_frame.from_num == kSetChannelPacketId);
    assert(first_from.id != 0);
    assert(first_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(first_from.queueStatus.mesh_packet_id == kSetChannelPacketId);
    assert(first_from.queueStatus.res == 0);
    const uint32_t first_from_radio_id = first_from.id;

    phone::meshtastic::MeshtasticBleFrame second_frame{};
    assert(admin_session.popToPhone(&second_frame));
    meshtastic_FromRadio second_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(second_frame, second_from));
    assert(second_from.id > first_from_radio_id);
    assert(second_from.which_payload_variant == meshtastic_FromRadio_packet_tag);
    assert(second_from.packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP);
    assert(second_from.packet.decoded.request_id == kSetChannelPacketId);
    meshtastic_AdminMessage admin_response = meshtastic_AdminMessage_init_zero;
    pb_istream_t admin_response_stream =
        pb_istream_from_buffer(second_from.packet.decoded.payload.bytes,
                               second_from.packet.decoded.payload.size);
    assert(pb_decode(&admin_response_stream, meshtastic_AdminMessage_fields, &admin_response));
    assert(admin_response.which_payload_variant == meshtastic_AdminMessage_get_channel_response_tag);
    assert(admin_response.get_channel_response.index == 1);
    assert(admin_response.get_channel_response.role == meshtastic_Channel_Role_SECONDARY);
    assert(std::strcmp(admin_response.get_channel_response.settings.name, "vic") == 0);
    assert(admin_response.get_channel_response.settings.psk.size == 32);
    assert(std::memcmp(admin_response.get_channel_response.settings.psk.bytes,
                       saved_admin_config.mesh.secondary_key,
                       32) == 0);
    assert(!admin_session.popToPhone(&second_frame));
    assert(admin_runtime.save_config_count == 1);

    phone::tests::FakePhoneRuntimeContext custom_runtime;
    FakeMeshtasticTransport custom_transport;
    FakeModuleConfigHooks custom_module_hooks;
    phone::meshtastic::MeshtasticPhoneSession custom_session(
        custom_runtime, custom_transport, nullptr, &custom_module_hooks, nullptr, nullptr, nullptr, nullptr);
    constexpr uint32_t kPrimaryCustomPacketId = 0xCAFE4400;
    uint8_t primary_channel_to_radio[meshtastic_ToRadio_size] = {};
    size_t primary_channel_to_radio_len = 0;
    assert(encodeAdminSetPrimaryCustomChannelToRadio(primary_channel_to_radio,
                                                     sizeof(primary_channel_to_radio),
                                                     primary_channel_to_radio_len,
                                                     kPrimaryCustomPacketId));
    assert(custom_session.handleToRadio(primary_channel_to_radio, primary_channel_to_radio_len));
    assert(custom_runtime.save_config_count == 0);
    assert(custom_runtime.apply_mesh_config_count == 1);
    const auto saved_custom_config = custom_runtime.getMeshtasticPhoneConfig();
    assert(saved_custom_config.primary_enabled);
    assert(std::strcmp(saved_custom_config.mesh.primary_channel_name, "Custom") == 0);
    assert(saved_custom_config.mesh.primary_channel_id == 0x10203040);
    assert(saved_custom_config.mesh.primary_key_len == 16);
    assert(saved_custom_config.primary_channel_has_module_settings);
    assert(saved_custom_config.primary_channel_position_precision == 32);
    assert(saved_custom_config.primary_channel_is_muted);
    uint8_t expected_short_psk[16] = {};
    size_t expected_short_psk_len = 0;
    chat::meshtastic::expandShortPsk(1, expected_short_psk, &expected_short_psk_len);
    assert(expected_short_psk_len == 16);
    assert(std::memcmp(saved_custom_config.mesh.primary_key, expected_short_psk, 16) == 0);

    phone::meshtastic::MeshtasticBleFrame custom_queue_frame{};
    assert(custom_session.popToPhone(&custom_queue_frame));
    meshtastic_FromRadio custom_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(custom_queue_frame, custom_from));
    assert(custom_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(custom_from.queueStatus.mesh_packet_id == kPrimaryCustomPacketId);
    assert(custom_from.queueStatus.res == 0);

    phone::meshtastic::MeshtasticBleFrame custom_response_frame{};
    assert(custom_session.popToPhone(&custom_response_frame));
    custom_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(custom_response_frame, custom_from));
    assert(custom_from.which_payload_variant == meshtastic_FromRadio_packet_tag);
    meshtastic_AdminMessage custom_response = meshtastic_AdminMessage_init_zero;
    pb_istream_t custom_response_stream =
        pb_istream_from_buffer(custom_from.packet.decoded.payload.bytes,
                               custom_from.packet.decoded.payload.size);
    assert(pb_decode(&custom_response_stream, meshtastic_AdminMessage_fields, &custom_response));
    assert(custom_response.which_payload_variant == meshtastic_AdminMessage_get_channel_response_tag);
    assert(custom_response.get_channel_response.index == 0);
    assert(custom_response.get_channel_response.role == meshtastic_Channel_Role_PRIMARY);
    assert(std::strcmp(custom_response.get_channel_response.settings.name, "Custom") == 0);
    assert(custom_response.get_channel_response.settings.id == 0x10203040);
    assert(custom_response.get_channel_response.settings.psk.size == 1);
    assert(custom_response.get_channel_response.settings.psk.bytes[0] == 1);
    assert(custom_response.get_channel_response.settings.has_module_settings);
    assert(custom_response.get_channel_response.settings.module_settings.position_precision == 32);
    assert(custom_response.get_channel_response.settings.module_settings.is_muted);
    assert(!custom_session.popToPhone(&custom_response_frame));
    assert(custom_runtime.save_config_count == 1);

    constexpr uint32_t kManualLoraPacketId = 0xCAFE4401;
    uint8_t manual_lora_to_radio[meshtastic_ToRadio_size] = {};
    size_t manual_lora_to_radio_len = 0;
    assert(encodeAdminSetManualLoraConfigToRadio(manual_lora_to_radio,
                                                 sizeof(manual_lora_to_radio),
                                                 manual_lora_to_radio_len,
                                                 kManualLoraPacketId));
    assert(custom_session.handleToRadio(manual_lora_to_radio, manual_lora_to_radio_len));
    assert(custom_runtime.save_config_count == 1);
    assert(custom_runtime.apply_mesh_config_count == 2);
    const auto saved_lora_config = custom_runtime.getMeshtasticPhoneConfig();
    assert(!saved_lora_config.mesh.use_preset);
    assert(saved_lora_config.mesh.bandwidth_khz == 62.0f);
    assert(saved_lora_config.mesh.spread_factor == 11);
    assert(saved_lora_config.mesh.coding_rate == 8);
    assert(saved_lora_config.mesh.region ==
           static_cast<uint8_t>(meshtastic_Config_LoRaConfig_RegionCode_US));
    assert(saved_lora_config.mesh.hop_limit == 5);
    assert(saved_lora_config.mesh.tx_enabled);
    assert(saved_lora_config.mesh.tx_power == 20);
    assert(saved_lora_config.mesh.channel_num == 7);
    assert(saved_lora_config.mesh.override_duty_cycle);
    assert(saved_lora_config.mesh.ignore_mqtt);
    assert(saved_lora_config.mesh.config_ok_to_mqtt);
    assert(saved_lora_config.mesh.override_frequency_mhz == 906.875f);

    phone::meshtastic::MeshtasticBleFrame lora_queue_frame{};
    assert(custom_session.popToPhone(&lora_queue_frame));
    meshtastic_FromRadio lora_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(lora_queue_frame, lora_from));
    assert(lora_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(lora_from.queueStatus.mesh_packet_id == kManualLoraPacketId);
    assert(lora_from.queueStatus.res == 0);

    phone::meshtastic::MeshtasticBleFrame lora_response_frame{};
    assert(custom_session.popToPhone(&lora_response_frame));
    lora_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(lora_response_frame, lora_from));
    assert(lora_from.which_payload_variant == meshtastic_FromRadio_packet_tag);
    meshtastic_AdminMessage lora_response = meshtastic_AdminMessage_init_zero;
    pb_istream_t lora_response_stream =
        pb_istream_from_buffer(lora_from.packet.decoded.payload.bytes,
                               lora_from.packet.decoded.payload.size);
    assert(pb_decode(&lora_response_stream, meshtastic_AdminMessage_fields, &lora_response));
    assert(lora_response.which_payload_variant == meshtastic_AdminMessage_get_config_response_tag);
    assert(lora_response.get_config_response.which_payload_variant == meshtastic_Config_lora_tag);
    assert(!lora_response.get_config_response.payload_variant.lora.use_preset);
    assert(lora_response.get_config_response.payload_variant.lora.bandwidth == 62);
    assert(lora_response.get_config_response.payload_variant.lora.spread_factor == 11);
    assert(lora_response.get_config_response.payload_variant.lora.coding_rate == 8);
    assert(lora_response.get_config_response.payload_variant.lora.override_frequency == 906.875f);
    assert(lora_response.get_config_response.payload_variant.lora.ignore_mqtt);
    assert(lora_response.get_config_response.payload_variant.lora.config_ok_to_mqtt);
    assert(!custom_session.popToPhone(&lora_response_frame));
    assert(custom_runtime.save_config_count == 2);

    phone::tests::FakePhoneRuntimeContext bluetooth_runtime;
    FakeMeshtasticTransport bluetooth_transport;
    FakeBluetoothConfigHooks bluetooth_hooks;
    phone::meshtastic::MeshtasticPhoneSession bluetooth_session(
        bluetooth_runtime, bluetooth_transport, &bluetooth_hooks, nullptr, nullptr, nullptr, nullptr, nullptr);
    constexpr uint32_t kBluetoothPacketId = 0xCAFE4403;
    uint8_t bluetooth_to_radio[meshtastic_ToRadio_size] = {};
    size_t bluetooth_to_radio_len = 0;
    assert(encodeAdminSetBluetoothConfigToRadio(bluetooth_to_radio,
                                                sizeof(bluetooth_to_radio),
                                                bluetooth_to_radio_len,
                                                kBluetoothPacketId));
    assert(bluetooth_runtime.ble_enabled);
    assert(bluetooth_session.handleToRadio(bluetooth_to_radio, bluetooth_to_radio_len));
    assert(bluetooth_runtime.ble_enabled);
    assert(bluetooth_hooks.save_count == 0);

    phone::meshtastic::MeshtasticBleFrame bluetooth_queue_frame{};
    assert(bluetooth_session.popToPhone(&bluetooth_queue_frame));
    meshtastic_FromRadio bluetooth_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(bluetooth_queue_frame, bluetooth_from));
    assert(bluetooth_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(bluetooth_from.queueStatus.mesh_packet_id == kBluetoothPacketId);
    assert(bluetooth_from.queueStatus.res == 0);

    phone::meshtastic::MeshtasticBleFrame bluetooth_response_frame{};
    assert(bluetooth_session.popToPhone(&bluetooth_response_frame));
    bluetooth_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(bluetooth_response_frame, bluetooth_from));
    assert(bluetooth_from.which_payload_variant == meshtastic_FromRadio_packet_tag);
    assert(bluetooth_from.packet.decoded.request_id == kBluetoothPacketId);
    meshtastic_AdminMessage bluetooth_response = meshtastic_AdminMessage_init_zero;
    pb_istream_t bluetooth_response_stream =
        pb_istream_from_buffer(bluetooth_from.packet.decoded.payload.bytes,
                               bluetooth_from.packet.decoded.payload.size);
    assert(pb_decode(&bluetooth_response_stream, meshtastic_AdminMessage_fields, &bluetooth_response));
    assert(bluetooth_response.which_payload_variant == meshtastic_AdminMessage_get_config_response_tag);
    assert(bluetooth_response.get_config_response.which_payload_variant == meshtastic_Config_bluetooth_tag);
    assert(!bluetooth_response.get_config_response.payload_variant.bluetooth.enabled);
    assert(bluetooth_runtime.ble_enabled);
    assert(bluetooth_hooks.save_count == 0);
    assert(!bluetooth_session.popToPhone(&bluetooth_response_frame));
    assert(!bluetooth_runtime.ble_enabled);
    assert(bluetooth_hooks.save_count == 1);
    assert(!bluetooth_hooks.last_saved.enabled);
    assert(bluetooth_hooks.last_saved.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN);
    assert(bluetooth_hooks.last_saved.fixed_pin == 0);

    phone::tests::FakePhoneRuntimeContext edit_runtime;
    FakeMeshtasticTransport edit_transport;
    phone::meshtastic::MeshtasticPhoneSession edit_session(
        edit_runtime, edit_transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    constexpr uint32_t kBeginEditPacketId = 0xCAFE5500;
    uint8_t begin_edit_to_radio[meshtastic_ToRadio_size] = {};
    size_t begin_edit_to_radio_len = 0;
    assert(encodeAdminBeginEditSettingsToRadio(begin_edit_to_radio,
                                               sizeof(begin_edit_to_radio),
                                               begin_edit_to_radio_len,
                                               kBeginEditPacketId));
    assert(edit_session.handleToRadio(begin_edit_to_radio, begin_edit_to_radio_len));

    phone::meshtastic::MeshtasticBleFrame edit_frame{};
    assert(edit_session.popToPhone(&edit_frame));
    meshtastic_FromRadio edit_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(edit_frame, edit_from));
    assert(edit_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(edit_from.queueStatus.mesh_packet_id == kBeginEditPacketId);
    assert(edit_from.queueStatus.res == 0);
    assert(!edit_session.popToPhone(&edit_frame));
    assert(edit_runtime.save_config_count == 0);

    constexpr uint32_t kEditSetChannelPacketId = 0xCAFE5501;
    uint8_t edit_channel_to_radio[meshtastic_ToRadio_size] = {};
    size_t edit_channel_to_radio_len = 0;
    assert(encodeAdminSetChannelToRadio(edit_channel_to_radio,
                                        sizeof(edit_channel_to_radio),
                                        edit_channel_to_radio_len,
                                        kEditSetChannelPacketId));
    assert(edit_session.handleToRadio(edit_channel_to_radio, edit_channel_to_radio_len));
    assert(edit_runtime.save_config_count == 0);
    assert(edit_runtime.apply_mesh_config_count == 1);

    assert(edit_session.popToPhone(&edit_frame));
    edit_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(edit_frame, edit_from));
    assert(edit_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(edit_from.queueStatus.mesh_packet_id == kEditSetChannelPacketId);
    assert(edit_from.queueStatus.res == 0);

    assert(edit_session.popToPhone(&edit_frame));
    edit_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(edit_frame, edit_from));
    assert(edit_from.which_payload_variant == meshtastic_FromRadio_packet_tag);
    assert(edit_from.packet.decoded.request_id == kEditSetChannelPacketId);
    assert(!edit_session.popToPhone(&edit_frame));
    assert(edit_runtime.save_config_count == 0);

    constexpr uint32_t kCommitEditPacketId = 0xCAFE5502;
    uint8_t commit_edit_to_radio[meshtastic_ToRadio_size] = {};
    size_t commit_edit_to_radio_len = 0;
    assert(encodeAdminCommitEditSettingsToRadio(commit_edit_to_radio,
                                                sizeof(commit_edit_to_radio),
                                                commit_edit_to_radio_len,
                                                kCommitEditPacketId));
    assert(edit_session.handleToRadio(commit_edit_to_radio, commit_edit_to_radio_len));
    assert(edit_runtime.save_config_count == 0);

    assert(edit_session.popToPhone(&edit_frame));
    edit_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(edit_frame, edit_from));
    assert(edit_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(edit_from.queueStatus.mesh_packet_id == kCommitEditPacketId);
    assert(edit_from.queueStatus.res == 0);
    assert(!edit_session.popToPhone(&edit_frame));
    assert(edit_runtime.save_config_count == 1);

    constexpr uint32_t kMqttPacketId = 0xCAFE4402;
    uint8_t mqtt_to_radio[meshtastic_ToRadio_size] = {};
    size_t mqtt_to_radio_len = 0;
    assert(encodeAdminSetMqttModuleConfigToRadio(mqtt_to_radio,
                                                 sizeof(mqtt_to_radio),
                                                 mqtt_to_radio_len,
                                                 kMqttPacketId));
    assert(custom_session.handleToRadio(mqtt_to_radio, mqtt_to_radio_len));
    assert(custom_module_hooks.save_count == 0);
    assert(custom_runtime.restart_device_count == 0);

    phone::meshtastic::MeshtasticBleFrame mqtt_queue_frame{};
    assert(custom_session.popToPhone(&mqtt_queue_frame));
    meshtastic_FromRadio mqtt_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(mqtt_queue_frame, mqtt_from));
    assert(mqtt_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(mqtt_from.queueStatus.mesh_packet_id == kMqttPacketId);
    assert(mqtt_from.queueStatus.res == 0);

    phone::meshtastic::MeshtasticBleFrame mqtt_response_frame{};
    assert(custom_session.popToPhone(&mqtt_response_frame));
    mqtt_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(mqtt_response_frame, mqtt_from));
    assert(mqtt_from.which_payload_variant == meshtastic_FromRadio_packet_tag);
    assert(mqtt_from.packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP);
    assert(mqtt_from.packet.decoded.request_id == kMqttPacketId);
    meshtastic_AdminMessage mqtt_response = meshtastic_AdminMessage_init_zero;
    pb_istream_t mqtt_response_stream =
        pb_istream_from_buffer(mqtt_from.packet.decoded.payload.bytes,
                               mqtt_from.packet.decoded.payload.size);
    assert(pb_decode(&mqtt_response_stream, meshtastic_AdminMessage_fields, &mqtt_response));
    assert(mqtt_response.which_payload_variant == meshtastic_AdminMessage_get_module_config_response_tag);
    assert(mqtt_response.get_module_config_response.which_payload_variant == meshtastic_ModuleConfig_mqtt_tag);
    const auto& mqtt_response_config = mqtt_response.get_module_config_response.payload_variant.mqtt;
    assert(mqtt_response_config.enabled);
    assert(!mqtt_response_config.proxy_to_client_enabled);
    assert(mqtt_response_config.encryption_enabled);
    assert(!mqtt_response_config.tls_enabled);
    assert(mqtt_response_config.map_reporting_enabled);
    assert(mqtt_response_config.has_map_report_settings);
    assert(mqtt_response_config.map_report_settings.publish_interval_secs == 3600);
    assert(mqtt_response_config.map_report_settings.position_precision == 16);
    assert(mqtt_response_config.map_report_settings.should_report_location);
    assert(std::strcmp(mqtt_response_config.address, "mqtt.example.test") == 0);
    assert(std::strcmp(mqtt_response_config.username, "trail") == 0);
    assert(std::strcmp(mqtt_response_config.password, "mate") == 0);
    assert(std::strcmp(mqtt_response_config.root, "msh") == 0);
    assert(custom_module_hooks.save_count == 0);
    assert(custom_runtime.restart_device_count == 0);
    assert(!custom_session.popToPhone(&mqtt_response_frame));
    assert(custom_module_hooks.save_count == 1);
    assert(custom_module_hooks.last_saved.has_mqtt);
    assert(custom_module_hooks.last_saved.mqtt.enabled);
    assert(custom_runtime.restart_device_count == 1);

    phone::tests::FakePhoneRuntimeContext config_runtime;
    phone::PhoneNodeView observation_only_node{};
    observation_only_node.node_id = 0x4A59CD8C;
    observation_only_node.last_seen = 123456789;
    observation_only_node.snr = 6.0f;
    observation_only_node.rssi = -69.0f;
    config_runtime.nodes.push_back(observation_only_node);

    phone::PhoneNodeView peer_node{};
    peer_node.node_id = 0x12345679;
    copyBounded(peer_node.long_name, sizeof(peer_node.long_name), "Peer Node");
    copyBounded(peer_node.short_name, sizeof(peer_node.short_name), "PN");
    config_runtime.nodes.push_back(peer_node);

    FakeMeshtasticTransport config_stage1_transport;
    phone::meshtastic::MeshtasticPhoneSession config_stage1_session(
        config_runtime, config_stage1_transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    constexpr uint32_t kConfigOnlyNonce = 0x00010F2C;
    uint8_t want_config_only_to_radio[meshtastic_ToRadio_size] = {};
    size_t want_config_only_to_radio_len = 0;
    assert(encodeWantConfigToRadio(want_config_only_to_radio,
                                   sizeof(want_config_only_to_radio),
                                   want_config_only_to_radio_len,
                                   kConfigOnlyNonce));
    assert(config_stage1_session.handleToRadio(want_config_only_to_radio, want_config_only_to_radio_len));

    phone::meshtastic::MeshtasticBleFrame config_only_frame{};
    bool saw_config_only_complete = false;
    bool saw_config_only_my_info = false;
    bool saw_config_only_deviceui = false;
    while (config_stage1_session.popToPhone(&config_only_frame))
    {
        meshtastic_FromRadio config_only_from = meshtastic_FromRadio_init_zero;
        assert(decodeFromRadio(config_only_frame, config_only_from));
        assert(config_only_from.which_payload_variant != meshtastic_FromRadio_node_info_tag);
        if (config_only_from.which_payload_variant == meshtastic_FromRadio_my_info_tag)
        {
            saw_config_only_my_info = true;
        }
        if (config_only_from.which_payload_variant == meshtastic_FromRadio_deviceuiConfig_tag)
        {
            saw_config_only_deviceui = true;
        }
        if (config_only_from.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
        {
            assert(config_only_frame.from_num == kConfigOnlyNonce);
            assert(config_only_from.config_complete_id == kConfigOnlyNonce);
            saw_config_only_complete = true;
            break;
        }
    }
    assert(saw_config_only_my_info);
    assert(saw_config_only_deviceui);
    assert(saw_config_only_complete);

    FakeMeshtasticTransport config_transport;
    phone::meshtastic::MeshtasticPhoneSession config_session(
        config_runtime, config_transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    constexpr uint32_t kConfigNonce = 0x00010F2D;
    uint8_t want_config_to_radio[meshtastic_ToRadio_size] = {};
    size_t want_config_to_radio_len = 0;
    assert(encodeWantConfigToRadio(want_config_to_radio,
                                   sizeof(want_config_to_radio),
                                   want_config_to_radio_len,
                                   kConfigNonce));
    assert(config_session.handleToRadio(want_config_to_radio, want_config_to_radio_len));

    phone::meshtastic::MeshtasticBleFrame config_frame{};
    assert(config_session.popToPhone(&config_frame));
    meshtastic_FromRadio config_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(config_frame, config_from));
    assert(config_frame.from_num == kConfigNonce);
    assert(config_from.id != 0);
    assert(config_from.which_payload_variant == meshtastic_FromRadio_node_info_tag);
    assert(config_from.node_info.num == config_runtime.self_node_id);
    assert(std::strcmp(config_from.node_info.user.id, "!12345678") == 0);
    assert(config_from.node_info.user.hw_model != meshtastic_HardwareModel_UNSET);

    bool saw_config_complete = false;
    bool saw_observation_only_node = false;
    bool saw_peer_node = false;
    uint32_t last_config_from_radio_id = config_from.id;
    while (config_session.popToPhone(&config_frame))
    {
        config_from = meshtastic_FromRadio_init_zero;
        assert(decodeFromRadio(config_frame, config_from));
        assert(config_from.id > last_config_from_radio_id);
        last_config_from_radio_id = config_from.id;
        assert(config_from.which_payload_variant != meshtastic_FromRadio_metadata_tag);
        assert(config_from.which_payload_variant != meshtastic_FromRadio_channel_tag);
        assert(config_from.which_payload_variant != meshtastic_FromRadio_config_tag);
        assert(config_from.which_payload_variant != meshtastic_FromRadio_moduleConfig_tag);
        assert(config_from.which_payload_variant != meshtastic_FromRadio_my_info_tag);
        assert(config_from.which_payload_variant != meshtastic_FromRadio_deviceuiConfig_tag);
        if (config_from.which_payload_variant == meshtastic_FromRadio_node_info_tag &&
            config_from.node_info.num == observation_only_node.node_id)
        {
            saw_observation_only_node = true;
        }
        if (config_from.which_payload_variant == meshtastic_FromRadio_node_info_tag &&
            config_from.node_info.num == peer_node.node_id)
        {
            assert(config_frame.from_num == peer_node.node_id);
            assert(config_from.node_info.has_user);
            assert(std::strcmp(config_from.node_info.user.short_name, peer_node.short_name) == 0);
            assert(std::strcmp(config_from.node_info.user.long_name, peer_node.long_name) == 0);
            saw_peer_node = true;
        }
        if (config_from.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
        {
            assert(config_frame.from_num == kConfigNonce);
            assert(config_from.config_complete_id == kConfigNonce);
            saw_config_complete = true;
            break;
        }
    }
    assert(!saw_observation_only_node);
    assert(saw_peer_node);
    assert(saw_config_complete);

    phone::meshcore::MeshCorePhoneCore meshcore_core(runtime, "Trail Mate");
    assert(!meshcore_core.handleRxFrame(nullptr, 0));
    const uint8_t unknown_meshcore_cmd[] = {2, 0, 0, 0};
    assert(meshcore_core.handleRxFrame(unknown_meshcore_cmd, sizeof(unknown_meshcore_cmd)));

    uint8_t out[172] = {};
    size_t out_len = 0;
    assert(meshcore_core.popTxFrame(out, &out_len));
    assert(out_len == 2);
    assert(out[0] == kMeshCoreResponseError);
    assert(out[1] == kMeshCoreErrorIllegalArg);
    return 0;
}
