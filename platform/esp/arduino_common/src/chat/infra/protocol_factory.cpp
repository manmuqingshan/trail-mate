/**
 * @file protocol_factory.cpp
 * @brief Factory for creating protocol-specific mesh adapters
 */

#include "platform/esp/arduino_common/chat/infra/protocol_factory.h"
#include "board/LoraBoard.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"

namespace chat
{

std::unique_ptr<IMeshAdapter> ProtocolFactory::createAdapter(MeshProtocol protocol,
                                                             LoraBoard& board,
                                                             IMeshPeerDirectory* peer_directory)
{
    switch (protocol)
    {
    case MeshProtocol::MeshCore:
        return std::unique_ptr<IMeshAdapter>(
            new chat::meshcore::MeshCoreAdapter(board, peer_directory));
    case MeshProtocol::RNode:
    case MeshProtocol::Reticulum:
        return std::unique_ptr<IMeshAdapter>(
            new chat::reticulum::ReticulumAdapter(board, peer_directory));
    case MeshProtocol::Meshtastic:
    default:
        return std::unique_ptr<IMeshAdapter>(
            new chat::meshtastic::MtAdapter(board, peer_directory));
    }
}

} // namespace chat
