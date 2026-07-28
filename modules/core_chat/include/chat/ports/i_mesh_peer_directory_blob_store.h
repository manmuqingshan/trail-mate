#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat
{

enum class MeshPeerDirectoryBlobLoadResult : uint8_t
{
    Loaded = 0,
    Missing = 1,
    Unavailable = 2,
    IoError = 3,
};

class IMeshPeerDirectoryBlobSink
{
  public:
    virtual ~IMeshPeerDirectoryBlobSink() = default;

    virtual bool begin(std::size_t expected_size) = 0;
    virtual bool write(const uint8_t* data, std::size_t len) = 0;
    virtual bool finish() = 0;
};

class IMeshPeerDirectoryBlobStore
{
  public:
    virtual ~IMeshPeerDirectoryBlobStore() = default;

    virtual MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>& out) = 0;
    virtual MeshPeerDirectoryBlobLoadResult loadBlobTo(
        IMeshPeerDirectoryBlobSink& sink);
    virtual bool saveBlob(const uint8_t* data, std::size_t len) = 0;
    virtual void clearBlob() = 0;
};

} // namespace chat
