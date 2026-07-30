#include "platform/esp/arduino_common/mesh/esp_meshtastic_adapter_bridge.h"

#include "platform/esp/arduino_common/app_tasks.h"

#include <Arduino.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>

namespace platform::esp::arduino_common::mesh
{

uint32_t EspArduinoMeshClock::nowMs() const
{
    return millis();
}

::mesh::CryptoResult EspArduinoMeshCryptoProvider::random(uint8_t* out, size_t len)
{
    if (!out && len > 0)
    {
        return ::mesh::CryptoResult::fail(::mesh::CryptoFailure::InvalidInput);
    }
    for (size_t index = 0; index < len; ++index)
    {
        out[index] = static_cast<uint8_t>(::random(0, 256));
    }
    return ::mesh::CryptoResult::success();
}

::mesh::CryptoResult EspArduinoMeshCryptoProvider::sha256(::mesh::ByteView input, uint8_t out[32])
{
    (void)input;
    (void)out;
    return ::mesh::CryptoResult::fail(::mesh::CryptoFailure::Unsupported);
}

::mesh::CryptoResult EspArduinoMeshCryptoProvider::aesCcmEncrypt(::mesh::ByteView key,
                                                                 ::mesh::ByteView nonce,
                                                                 ::mesh::ByteView aad,
                                                                 ::mesh::ByteView plaintext,
                                                                 uint8_t* ciphertext,
                                                                 size_t ciphertext_capacity,
                                                                 size_t& ciphertext_size)
{
    (void)key;
    (void)nonce;
    (void)aad;
    (void)plaintext;
    (void)ciphertext;
    (void)ciphertext_capacity;
    ciphertext_size = 0;
    return ::mesh::CryptoResult::fail(::mesh::CryptoFailure::Unsupported);
}

::mesh::CryptoResult EspArduinoMeshCryptoProvider::aesCcmDecrypt(::mesh::ByteView key,
                                                                 ::mesh::ByteView nonce,
                                                                 ::mesh::ByteView aad,
                                                                 ::mesh::ByteView ciphertext,
                                                                 uint8_t* plaintext,
                                                                 size_t plaintext_capacity,
                                                                 size_t& plaintext_size)
{
    (void)key;
    (void)nonce;
    (void)aad;
    (void)ciphertext;
    (void)plaintext;
    (void)plaintext_capacity;
    plaintext_size = 0;
    return ::mesh::CryptoResult::fail(::mesh::CryptoFailure::Unsupported);
}

EspMeshtasticPacketRadio::EspMeshtasticPacketRadio(LoraBoard& board)
    : board_(board)
{
}

::mesh::RadioResult EspMeshtasticPacketRadio::configure(const ::mesh::RadioConfig& config)
{
    (void)config;
    return board_.isRadioOnline()
               ? ::mesh::RadioResult::success()
               : ::mesh::RadioResult::fail(::mesh::RadioFailure::NotReady);
}

::mesh::RadioResult EspMeshtasticPacketRadio::send(::mesh::ByteView packet)
{
    if (packet.empty())
    {
        return ::mesh::RadioResult::fail(::mesh::RadioFailure::InvalidConfig);
    }
    if (packet.size > 255)
    {
        return ::mesh::RadioResult::fail(::mesh::RadioFailure::InvalidConfig);
    }
    if (!board_.isRadioOnline())
    {
        return ::mesh::RadioResult::fail(::mesh::RadioFailure::NotReady);
    }

    if (app::AppTasks::enqueueRadioTransmit(packet.data, packet.size))
    {
        last_sent_size_ = packet.size;
        std::memcpy(last_sent_, packet.data, packet.size);
        return ::mesh::RadioResult::success();
    }
    return ::mesh::RadioResult::fail(::mesh::RadioFailure::TxFailed);
}

bool EspMeshtasticPacketRadio::poll(::mesh::RadioRxPacket& out)
{
    (void)out;
    return false;
}

void EspMeshEventBridge::emit(const ::mesh::MeshEvent& event)
{
    last_event = event;
    ++emitted_count;
}

EspMeshPeerDirectoryPeerKeyStore::EspMeshPeerDirectoryPeerKeyStore(
    ::chat::IMeshPeerDirectory* directory)
    : directory_(directory)
{
}

::mesh::StoreResult EspMeshPeerDirectoryPeerKeyStore::get(
    ::mesh::NodeId node_id,
    ::mesh::PeerPublicKey& out)
{
    if (!directory_ || !node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
    }

    ::chat::MeshPeerRecord record{};
    const auto status = directory_->find(
        ::chat::makeMeshPeerNodeIdentity(::chat::MeshProtocol::Meshtastic,
                                         node_id.value),
        record);
    if (!status.succeeded() || !record.meshtastic.has_public_key)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
    }

    out = {};
    out.node_id = node_id;
    std::memcpy(out.public_key,
                record.meshtastic.public_key,
                sizeof(out.public_key));
    out.updated_at_ms = record.last_seen_s * 1000UL;
    out.verified = record.meshtastic.key_manually_verified;
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult EspMeshPeerDirectoryPeerKeyStore::put(
    const ::mesh::PeerPublicKey& key)
{
    if (!directory_ || !key.node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    ::chat::MeshPeerRecord record{};
    record.valid = true;
    record.identity = ::chat::makeMeshPeerNodeIdentity(
        ::chat::MeshProtocol::Meshtastic,
        key.node_id.value);
    record.source = ::chat::MeshPeerSource::RuntimeRx;
    record.first_seen_s = key.updated_at_ms / 1000UL;
    record.last_seen_s = record.first_seen_s;
    record.meshtastic.has_public_key = true;
    record.meshtastic.key_manually_verified = key.verified;
    std::memcpy(record.meshtastic.public_key,
                key.public_key,
                sizeof(record.meshtastic.public_key));

    const auto status = directory_->record(record);
    return status.succeeded()
               ? ::mesh::StoreResult::success()
               : ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
}

::mesh::StoreResult EspMeshPeerDirectoryPeerKeyStore::remove(
    ::mesh::NodeId node_id)
{
    if (!directory_ || !node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }
    const auto status = directory_->remove(
        ::chat::makeMeshPeerNodeIdentity(::chat::MeshProtocol::Meshtastic,
                                         node_id.value));
    if (status.succeeded() ||
        status.code == ::chat::MeshPeerDirectoryStatusCode::NotFound)
    {
        return ::mesh::StoreResult::success();
    }
    return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
}

::mesh::StoreResult EspMeshPeerDirectoryPeerKeyStore::clear()
{
    if (!directory_)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }
    const auto status = directory_->clearProtocol(::chat::MeshProtocol::Meshtastic);
    return status.succeeded()
               ? ::mesh::StoreResult::success()
               : ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
}

EspMeshtasticAdapterBridge::EspMeshtasticAdapterBridge(
    LoraBoard& board,
    ::chat::IMeshPeerDirectory* peer_directory)
    : radio_(board),
      peer_store_(peer_directory),
      identity_(local_store_, peer_store_, crypto_, clock_),
      receive_(protocol_, identity_, events_, clock_, &receive_dedup_),
      direct_(protocol_, identity_, radio_, clock_, events_),
      session_(radio_, protocol_, direct_, receive_, clock_)
{
    ::mesh::MeshRuntimeConfig config{};
    config.protocol = ::mesh::MeshProtocolKind::Meshtastic;
    config.radio.frequency_hz = 1;
    (void)session_.start(config);
}

void* EspMeshtasticAdapterBridge::operator new(std::size_t size)
{
    void* ptr = heap_caps_malloc_prefer(size,
                                        2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return ptr != nullptr ? ptr : ::operator new(size);
}

void EspMeshtasticAdapterBridge::operator delete(void* ptr) noexcept
{
    heap_caps_free(ptr);
}

void EspMeshtasticAdapterBridge::operator delete(void* ptr, std::size_t) noexcept
{
    operator delete(ptr);
}

::mesh::SendResult EspMeshtasticAdapterBridge::sendDirect(const ::mesh::DirectMessageCommand& command)
{
    if (session_.state() != ::mesh::MeshSessionState::Ready)
    {
        ::mesh::MeshRuntimeConfig config{};
        config.protocol = ::mesh::MeshProtocolKind::Meshtastic;
        config.radio.frequency_hz = 1;
        if (!session_.start(config))
        {
            return ::mesh::SendResult::fail(::mesh::SendFailure::NotReady);
        }
    }
    return session_.sendDirect(command);
}

bool EspMeshtasticAdapterBridge::copyLastSentPacket(uint8_t* out, size_t capacity, size_t& out_size) const
{
    if (!out || radio_.last_sent_size_ == 0 || capacity < radio_.last_sent_size_)
    {
        return false;
    }
    std::memcpy(out, radio_.last_sent_, radio_.last_sent_size_);
    out_size = radio_.last_sent_size_;
    return true;
}

void EspMeshtasticAdapterBridge::onRadioPacket(const uint8_t* data,
                                               size_t size,
                                               int16_t rssi,
                                               int8_t snr)
{
    if (!data || size == 0 || size > sizeof(::mesh::RadioRxPacket::bytes))
    {
        return;
    }

    std::memcpy(rx_packet_scratch_.bytes, data, size);
    rx_packet_scratch_.size = size;
    rx_packet_scratch_.rssi = rssi;
    rx_packet_scratch_.snr = snr;
    rx_packet_scratch_.received_at_ms = clock_.nowMs();
    receive_.onRadioPacket(rx_packet_scratch_);
    rx_packet_scratch_ = ::mesh::RadioRxPacket{};
}

void EspMeshtasticAdapterBridge::tick()
{
    session_.tick();
}

} // namespace platform::esp::arduino_common::mesh
