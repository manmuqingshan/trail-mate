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
    Busy = 7,
    DeviceUnavailable = 8,
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
    constexpr MeshPeerDirectoryCapacity(std::size_t persisted = 0,
                                        std::size_t hot_cache = 0)
        : persisted_records(persisted), hot_cache_records(hot_cache)
    {
    }

    std::size_t persisted_records;
    std::size_t hot_cache_records;
};

enum class MeshPeerDirectoryView : uint8_t
{
    All = 0,
    Contacts = 1,
    Nearby = 2,
    Ignored = 3,
};

class IMeshPeerDirectoryVisitor
{
  public:
    virtual ~IMeshPeerDirectoryVisitor() = default;
    virtual bool visit(const MeshPeerRecord& record) = 0;
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
    virtual MeshPeerDirectoryStatus visit(
        MeshProtocol protocol,
        MeshPeerDirectoryView view,
        IMeshPeerDirectoryVisitor& visitor) = 0;
    virtual MeshPeerDirectoryStatus setUserAlias(
        const MeshPeerIdentity& identity,
        const char* alias) = 0;
    virtual MeshPeerDirectoryStatus setUserFlags(
        const MeshPeerIdentity& identity,
        const MeshPeerUserFlags& flags) = 0;
    virtual MeshPeerDirectoryStatus setKeyManuallyVerified(
        const MeshPeerIdentity& identity,
        bool verified) = 0;
    virtual MeshPeerDirectoryStatus remove(const MeshPeerIdentity& identity) = 0;
    virtual MeshPeerDirectoryStatus clearProtocol(MeshProtocol protocol) = 0;
    virtual MeshPeerDirectoryCapacity capacityFor(MeshProtocol protocol) const = 0;
    virtual MeshPeerDirectoryStatus flush() = 0;
};

} // namespace chat
