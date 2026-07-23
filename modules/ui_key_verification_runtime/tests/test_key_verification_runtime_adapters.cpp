#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/contact_service.h"
#include "ui_key_verification_runtime/key_verification_action_sink.h"
#include "ui_key_verification_runtime/key_verification_presentation_source.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace
{

class FakeMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    bool sendText(::chat::ChannelId,
                  const std::string&,
                  ::chat::MessageId*,
                  ::chat::NodeId = 0) override
    {
        return false;
    }

    bool pollIncomingText(::chat::MeshIncomingText*) override { return false; }

    bool sendAppData(::chat::ChannelId,
                     uint32_t,
                     const uint8_t*,
                     size_t,
                     ::chat::NodeId = 0,
                     bool = false,
                     ::chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }

    bool pollIncomingData(::chat::MeshIncomingData*) override { return false; }
    void applyConfig(const ::chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }

    bool startKeyVerification(::chat::NodeId dest) override
    {
        ++start_count;
        last_start_peer = dest;
        return start_ok;
    }

    bool submitKeyVerificationNumber(::chat::NodeId dest,
                                     uint64_t nonce,
                                     uint32_t number) override
    {
        ++submit_count;
        last_submit_peer = dest;
        last_nonce = nonce;
        last_number = number;
        return submit_ok;
    }

    ::chat::MeshProtocol backendProtocol() const override
    {
        return protocol;
    }

    bool start_ok = true;
    bool submit_ok = true;
    int start_count = 0;
    int submit_count = 0;
    ::chat::NodeId last_start_peer = 0;
    ::chat::NodeId last_submit_peer = 0;
    uint64_t last_nonce = 0;
    uint32_t last_number = 0;
    ::chat::MeshProtocol protocol = ::chat::MeshProtocol::MeshCore;
};

class MemoryPeerDirectoryBlobStore final
    : public ::chat::IMeshPeerDirectoryBlobStore
{
  public:
    ::chat::MeshPeerDirectoryBlobLoadResult loadBlob(
        std::vector<uint8_t>& out) override
    {
        out = bytes_;
        return bytes_.empty()
                   ? ::chat::MeshPeerDirectoryBlobLoadResult::Missing
                   : ::chat::MeshPeerDirectoryBlobLoadResult::Loaded;
    }

    bool saveBlob(const uint8_t* data, std::size_t len) override
    {
        bytes_.assign(data, data + len);
        return true;
    }

    void clearBlob() override { bytes_.clear(); }

  private:
    std::vector<uint8_t> bytes_{};
};

} // namespace

int main()
{
    using namespace ui::key_verification;

    MemoryPeerDirectoryBlobStore blob_store;
    ::chat::MeshPeerDirectoryCore directory(blob_store);
    ::chat::contacts::ContactService contacts(directory);
    contacts.begin();
    contacts.updateNodeInfo(0x1234, "ADA", "Ada Lovelace", 0.0F, 0.0F, 1);
    ::chat::MeshPeerRecord keyed_peer{};
    keyed_peer.valid = true;
    keyed_peer.identity = ::chat::makeMeshPeerNodeIdentity(
        ::chat::MeshProtocol::Meshtastic,
        0x1234);
    keyed_peer.last_seen_s = 1;
    keyed_peer.meshtastic.has_public_key = true;
    keyed_peer.meshtastic.public_key[0] = 0x42;
    assert(directory.record(keyed_peer).succeeded());
    assert(contacts.addContact(0x1234, "Ada"));

    FakeMeshAdapter mesh;
    ui_key_verification_runtime::KeyVerificationSessionAdapter session{};
    ui_key_verification_runtime::KeyVerificationPresentationSource source(
        session,
        &contacts,
        &mesh);
    ui_key_verification_runtime::KeyVerificationActionSink sink(session,
                                                                &mesh,
                                                                &contacts);

    source.onNumberRequest(0x1234, 99);
    KeyVerificationSnapshot snapshot{};
    assert(source.buildKeyVerificationSnapshot(
        KeyVerificationRequest{VerificationProtocol::Unknown, 0x1234},
        snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.protocol == VerificationProtocol::MeshCore);
    assert(snapshot.state == VerificationState::Pending);
    assert(snapshot.prompt == VerificationPromptKind::EnterNumber);
    assert(snapshot.requires_number);
    assert(snapshot.can_reject);
    assert(std::strcmp(snapshot.peer_label.c_str(), "Ada") == 0);
    assert(std::strcmp(snapshot.status_line.c_str(),
                       "Enter verification number") == 0);

    auto result = sink.submitNumber(0x1234, 654321);
    assert(result.ok);
    assert(mesh.submit_count == 1);
    assert(mesh.last_submit_peer == 0x1234);
    assert(mesh.last_nonce == 99);
    assert(mesh.last_number == 654321);
    assert(session.prompt == VerificationPromptKind::None);

    source.onNumberInform(0x1234, 100, 123456);
    assert(source.buildKeyVerificationSnapshot(
        KeyVerificationRequest{VerificationProtocol::Unknown, 0x1234},
        snapshot));
    assert(snapshot.prompt == VerificationPromptKind::ShowNumber);
    assert(snapshot.can_copy_code);
    assert(std::strcmp(snapshot.verification_code.c_str(), "123 456") == 0);

    source.onFinal(0x1234, 101, false, "ABCD-1234");
    assert(source.buildKeyVerificationSnapshot(
        KeyVerificationRequest{VerificationProtocol::Unknown, 0x1234},
        snapshot));
    assert(snapshot.prompt == VerificationPromptKind::CompareCode);
    assert(snapshot.can_accept);
    assert(snapshot.can_reject);
    assert(snapshot.can_copy_code);
    assert(std::strcmp(snapshot.verification_code.c_str(), "ABCD-1234") == 0);

    result = sink.accept(0x1234);
    assert(result.ok);
    assert(session.state == VerificationState::Verified);
    const auto* verified_peer = contacts.getPeerByNodeId(0x1234);
    assert(verified_peer && verified_peer->key_manually_verified);

    source.onFinal(0x1234, 102, true, "WXYZ-9876");
    result = sink.copyCode(0x1234);
    assert(!result.ok);
    assert(result.failure == ui::UiActionFailure::Unsupported);

    result = sink.reject(0x1234);
    assert(result.ok);
    assert(session.state == VerificationState::Rejected);

    result = sink.refresh(0x1234);
    assert(result.ok);
    assert(mesh.start_count == 1);
    assert(mesh.last_start_peer == 0x1234);
    assert(session.state == VerificationState::Pending);

    source.onPeerKeyMissing(0xBEEF);
    assert(source.buildKeyVerificationSnapshot(
        KeyVerificationRequest{VerificationProtocol::Unknown, 0xBEEF},
        snapshot));
    assert(snapshot.state == VerificationState::Failed);
    assert(snapshot.failure == VerificationFailureKind::MissingPeerKey);
    assert(snapshot.can_refresh);

    ui_key_verification_runtime::KeyVerificationActionSink no_runtime_sink(
        session,
        nullptr,
        nullptr);
    result = no_runtime_sink.refresh(0xBEEF);
    assert(!result.ok);
    assert(result.failure == ui::UiActionFailure::NotReady);

    result = no_runtime_sink.submitNumber(0xBEEF, 1000000);
    assert(!result.ok);
    assert(result.failure == ui::UiActionFailure::InvalidInput);

    source.clear();
    assert(source.buildKeyVerificationSnapshot(
        KeyVerificationRequest{VerificationProtocol::Meshtastic, 0xBEEF},
        snapshot));
    assert(snapshot.state == VerificationState::Idle);
    assert(snapshot.can_refresh);

    return 0;
}
