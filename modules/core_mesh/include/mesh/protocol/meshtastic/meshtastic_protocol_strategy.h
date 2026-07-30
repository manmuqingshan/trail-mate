#pragma once

#include "mesh/protocol/mesh_protocol_strategy.h"
#include "meshtastic/mesh.pb.h"

namespace mesh
{
namespace meshtastic
{

class MeshtasticProtocolStrategy final : public MeshProtocolStrategy
{
  public:
    MeshProtocolKind kind() const override;
    RadioConfig deriveRadioConfig(const MeshRuntimeConfig& config) override;
    ProtocolResult buildDirectMessage(const ProtocolBuildContext& context,
                                      const DirectMessageCommand& command,
                                      EncodedPacket& out) override;
    ProtocolResult parseRadioPacket(const RadioRxPacket& packet,
                                    MeshProtocolEvent& out) override;

  private:
    meshtastic_Data data_scratch_ = meshtastic_Data_init_default;
};

} // namespace meshtastic
} // namespace mesh
