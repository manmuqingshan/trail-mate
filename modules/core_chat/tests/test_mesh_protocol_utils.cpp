#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"

#include <cassert>
#include <cstring>

namespace
{

bool same_text(const char* lhs, const char* rhs)
{
    return std::strcmp(lhs, rhs) == 0;
}

} // namespace

int main()
{
    assert(chat::infra::normalizeMeshProtocol(chat::MeshProtocol::RNode) ==
           chat::MeshProtocol::Reticulum);
    assert(chat::infra::meshProtocolFromRaw(
               static_cast<uint8_t>(chat::MeshProtocol::RNode)) ==
           chat::MeshProtocol::Reticulum);
    assert(same_text(chat::infra::meshProtocolName(chat::MeshProtocol::RNode),
                     "Reticulum"));
    assert(same_text(chat::infra::meshProtocolShortName(chat::MeshProtocol::RNode),
                     "RT"));
    assert(same_text(chat::infra::meshProtocolSlug(chat::MeshProtocol::RNode),
                     "reticulum"));

    assert(chat::infra::isValidNodeProtocol(
        chat::contacts::NodeProtocolType::Meshtastic));
    assert(chat::infra::isValidNodeProtocol(
        chat::contacts::NodeProtocolType::RNode));
    assert(chat::infra::isValidNodeProtocol(
        chat::contacts::NodeProtocolType::LXMF));
    assert(chat::infra::isReticulumNodeProtocol(
        chat::contacts::NodeProtocolType::RNode));
    assert(chat::infra::isReticulumNodeProtocol(
        chat::contacts::NodeProtocolType::LXMF));
    assert(chat::infra::normalizeNodeProtocol(
               chat::contacts::NodeProtocolType::RNode) ==
           chat::contacts::NodeProtocolType::Reticulum);
    assert(chat::infra::meshProtocolFromNodeProtocol(
               chat::contacts::NodeProtocolType::RNode) ==
           chat::MeshProtocol::Reticulum);
    assert(chat::infra::meshProtocolFromNodeProtocol(
               chat::contacts::NodeProtocolType::LXMF) ==
           chat::MeshProtocol::Reticulum);
    assert(same_text(chat::infra::nodeProtocolName(
                         chat::contacts::NodeProtocolType::RNode),
                     "Reticulum"));
    assert(same_text(chat::infra::nodeProtocolName(
                         chat::contacts::NodeProtocolType::LXMF),
                     "Reticulum"));
    assert(same_text(chat::infra::nodeProtocolShortName(
                         chat::contacts::NodeProtocolType::RNode),
                     "RT"));
    assert(same_text(chat::infra::nodeProtocolShortName(
                         chat::contacts::NodeProtocolType::LXMF),
                     "RT"));

    const auto invalid =
        static_cast<chat::contacts::NodeProtocolType>(0xFE);
    assert(!chat::infra::isValidNodeProtocol(invalid));
    assert(chat::infra::meshProtocolFromNodeProtocol(
               invalid,
               chat::MeshProtocol::MeshCore) == chat::MeshProtocol::MeshCore);
    assert(same_text(chat::infra::nodeProtocolName(invalid), "Unknown"));
    assert(same_text(chat::infra::nodeProtocolShortName(invalid), ""));

    const chat::MeshConfig mesh_config{};
    for (const auto& group : mesh_config.reticulum_groups)
    {
        assert(!group.enabled);
        assert(group.name[0] == '\0');
        assert(!chat::hasReticulumDestinationIdentity(group.identity));
    }

    return 0;
}
