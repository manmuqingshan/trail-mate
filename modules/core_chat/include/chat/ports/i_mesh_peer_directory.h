#pragma once

#include "chat/domain/mesh_peer_directory.h"

#include <cstddef>
#include <cstdint>

namespace chat
{

enum class MeshPeerDirectoryStatusCode : uint8_t
{
    Ok = 0,
    NotFound = 1,
    InvalidArgument = 2,
    StorageUnavailable = 3,
    IoError = 4,
    CapacityExceeded = 5,
    Unsupported = 6,
};

struct MeshPeerDirectoryStatus
{
    MeshPeerDirectoryStatusCode code = MeshPeerDirectoryStatusCode::Ok;

    bool succeeded() const
    {
        return code == MeshPeerDirectoryStatusCode::Ok;
    }

    static MeshPeerDirectoryStatus success()
    {
        return MeshPeerDirectoryStatus{};
    }

    static MeshPeerDirectoryStatus fail(MeshPeerDirectoryStatusCode failure)
    {
        MeshPeerDirectoryStatus status{};
        status.code = failure == MeshPeerDirectoryStatusCode::Ok
                          ? MeshPeerDirectoryStatusCode::InvalidArgument
                          : failure;
        return status;
    }
};

struct MeshPeerDirectoryCapacity
{
    std::size_t persisted_records = 0;
    std::size_t hot_cache_records = 0;
};

class IMeshPeerDirectory
{
  public:
    virtual ~IMeshPeerDirectory() = default;

    virtual MeshPeerDirectoryStatus begin() = 0;
    virtual MeshPeerDirectoryStatus record(const MeshPeerRecord& record) = 0;
    virtual MeshPeerDirectoryStatus find(const MeshPeerIdentity& identity,
                                         MeshPeerRecord& out_record) = 0;
    virtual MeshPeerDirectoryStatus findByNodeId(MeshProtocol protocol,
                                                 NodeId node_id,
                                                 MeshPeerRecord& out_record) = 0;
    virtual MeshPeerDirectoryStatus loadRecent(MeshProtocol protocol,
                                               MeshPeerRecord* out_records,
                                               std::size_t max_records,
                                               std::size_t* out_count) = 0;
    virtual MeshPeerDirectoryStatus search(MeshProtocol protocol,
                                           const char* query,
                                           MeshPeerRecord* out_records,
                                           std::size_t max_records,
                                           std::size_t* out_count) = 0;
    virtual MeshPeerDirectoryStatus setUserFlags(
        const MeshPeerIdentity& identity,
        const MeshPeerUserFlags& flags) = 0;
    virtual MeshPeerDirectoryStatus remove(const MeshPeerIdentity& identity) = 0;
    virtual MeshPeerDirectoryStatus clearProtocol(MeshProtocol protocol) = 0;
    virtual MeshPeerDirectoryCapacity capacityFor(MeshProtocol protocol) const = 0;
    virtual MeshPeerDirectoryStatus flush() = 0;
};

} // namespace chat
