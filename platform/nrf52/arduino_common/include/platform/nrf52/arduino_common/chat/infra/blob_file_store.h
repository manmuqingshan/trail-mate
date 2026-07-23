#pragma once

#include "chat/ports/i_mesh_peer_directory_blob_store.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform::nrf52::arduino_common::chat::infra
{

class BlobFileStore
{
  public:
    BlobFileStore(const char* path,
                  uint32_t magic,
                  uint16_t version,
                  std::size_t max_payload_bytes);

    bool loadBlob(std::vector<uint8_t>& out);
    bool saveBlob(const uint8_t* data, size_t len);
    void clearBlob();

  private:
    struct FileHeader
    {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t reserved = 0;
        uint32_t payload_len = 0;
        uint32_t crc = 0;
    } __attribute__((packed));

    bool ensureFs() const;
    static uint32_t computeCrc(const uint8_t* data, size_t len);

    const char* path_ = nullptr;
    uint32_t magic_ = 0;
    uint16_t version_ = 0;
    std::size_t max_payload_bytes_ = 0;
};

class MeshPeerDirectoryBlobFileStore final
    : public ::chat::IMeshPeerDirectoryBlobStore
{
  public:
    explicit MeshPeerDirectoryBlobFileStore(const char* path)
        : store_(path, 0x5244504DUL, 1, 256U * 1024U)
    {
    }

    ::chat::MeshPeerDirectoryBlobLoadResult loadBlob(
        std::vector<uint8_t>& out) override;
    bool saveBlob(const uint8_t* data, size_t len) override;
    void clearBlob() override { store_.clearBlob(); }

  private:
    BlobFileStore store_;
};

} // namespace platform::nrf52::arduino_common::chat::infra
