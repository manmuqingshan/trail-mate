/**
 * @file mesh_protocol_utils.cpp
 * @brief Shared MeshProtocol validation and label helpers
 */

#include "chat/infra/mesh_protocol_utils.h"

namespace chat::infra
{

bool isValidMeshProtocol(MeshProtocol protocol)
{
    switch (protocol)
    {
    case MeshProtocol::Meshtastic:
    case MeshProtocol::MeshCore:
    case MeshProtocol::RNode:
    case MeshProtocol::Reticulum:
        return true;
    default:
        return false;
    }
}

bool isValidMeshProtocolValue(uint8_t raw)
{
    return isValidMeshProtocol(static_cast<MeshProtocol>(raw));
}

bool isReticulumMeshProtocol(MeshProtocol protocol)
{
    return protocol == MeshProtocol::RNode || protocol == MeshProtocol::Reticulum;
}

MeshProtocol normalizeMeshProtocol(MeshProtocol protocol)
{
    return isReticulumMeshProtocol(protocol) ? MeshProtocol::Reticulum : protocol;
}

MeshProtocol meshProtocolFromRaw(uint8_t raw, MeshProtocol fallback)
{
    const MeshProtocol protocol = static_cast<MeshProtocol>(raw);
    return isValidMeshProtocol(protocol) ? normalizeMeshProtocol(protocol) : fallback;
}

const char* meshProtocolName(MeshProtocol protocol)
{
    switch (normalizeMeshProtocol(protocol))
    {
    case MeshProtocol::MeshCore:
        return "MeshCore";
    case MeshProtocol::Reticulum:
        return "Reticulum";
    case MeshProtocol::Meshtastic:
    default:
        return "Meshtastic";
    }
}

const char* meshProtocolShortName(MeshProtocol protocol)
{
    switch (normalizeMeshProtocol(protocol))
    {
    case MeshProtocol::MeshCore:
        return "MC";
    case MeshProtocol::Reticulum:
        return "RT";
    case MeshProtocol::Meshtastic:
    default:
        return "MT";
    }
}

const char* meshProtocolSlug(MeshProtocol protocol)
{
    switch (normalizeMeshProtocol(protocol))
    {
    case MeshProtocol::MeshCore:
        return "meshcore";
    case MeshProtocol::Reticulum:
        return "reticulum";
    case MeshProtocol::Meshtastic:
    default:
        return "meshtastic";
    }
}

bool isReticulumNodeProtocol(contacts::NodeProtocolType protocol)
{
    return protocol == contacts::NodeProtocolType::RNode ||
           protocol == contacts::NodeProtocolType::Reticulum;
}

bool isValidNodeProtocol(contacts::NodeProtocolType protocol)
{
    switch (protocol)
    {
    case contacts::NodeProtocolType::Unknown:
    case contacts::NodeProtocolType::Meshtastic:
    case contacts::NodeProtocolType::MeshCore:
    case contacts::NodeProtocolType::RNode:
    case contacts::NodeProtocolType::Reticulum:
        return true;
    default:
        return false;
    }
}

contacts::NodeProtocolType normalizeNodeProtocol(contacts::NodeProtocolType protocol)
{
    return isReticulumNodeProtocol(protocol)
               ? contacts::NodeProtocolType::Reticulum
               : protocol;
}

MeshProtocol meshProtocolFromNodeProtocol(contacts::NodeProtocolType protocol,
                                          MeshProtocol fallback)
{
    if (!isValidNodeProtocol(protocol))
    {
        return fallback;
    }

    switch (normalizeNodeProtocol(protocol))
    {
    case contacts::NodeProtocolType::Meshtastic:
        return MeshProtocol::Meshtastic;
    case contacts::NodeProtocolType::MeshCore:
        return MeshProtocol::MeshCore;
    case contacts::NodeProtocolType::Reticulum:
        return MeshProtocol::Reticulum;
    case contacts::NodeProtocolType::Unknown:
    default:
        return fallback;
    }
}

const char* nodeProtocolName(contacts::NodeProtocolType protocol)
{
    if (!isValidNodeProtocol(protocol))
    {
        return "Unknown";
    }

    switch (normalizeNodeProtocol(protocol))
    {
    case contacts::NodeProtocolType::MeshCore:
        return "MeshCore";
    case contacts::NodeProtocolType::Reticulum:
        return "Reticulum";
    case contacts::NodeProtocolType::Meshtastic:
        return "Meshtastic";
    case contacts::NodeProtocolType::Unknown:
    default:
        return "Unknown";
    }
}

const char* nodeProtocolShortName(contacts::NodeProtocolType protocol)
{
    if (!isValidNodeProtocol(protocol))
    {
        return "";
    }

    switch (normalizeNodeProtocol(protocol))
    {
    case contacts::NodeProtocolType::MeshCore:
        return "MC";
    case contacts::NodeProtocolType::Reticulum:
        return "RT";
    case contacts::NodeProtocolType::Meshtastic:
        return "MT";
    case contacts::NodeProtocolType::Unknown:
    default:
        return "";
    }
}

} // namespace chat::infra
