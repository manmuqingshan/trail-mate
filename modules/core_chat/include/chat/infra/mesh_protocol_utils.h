/**
 * @file mesh_protocol_utils.h
 * @brief Shared MeshProtocol validation and label helpers
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/domain/contact_types.h"

namespace chat::infra
{

bool isValidMeshProtocol(MeshProtocol protocol);
bool isValidMeshProtocolValue(uint8_t raw);
bool isReticulumMeshProtocol(MeshProtocol protocol);
MeshProtocol normalizeMeshProtocol(MeshProtocol protocol);
MeshProtocol meshProtocolFromRaw(uint8_t raw,
                                 MeshProtocol fallback = MeshProtocol::Meshtastic);
const char* meshProtocolName(MeshProtocol protocol);
const char* meshProtocolShortName(MeshProtocol protocol);
const char* meshProtocolSlug(MeshProtocol protocol);
bool isValidNodeProtocol(contacts::NodeProtocolType protocol);
bool isReticulumNodeProtocol(contacts::NodeProtocolType protocol);
contacts::NodeProtocolType normalizeNodeProtocol(contacts::NodeProtocolType protocol);
MeshProtocol meshProtocolFromNodeProtocol(
    contacts::NodeProtocolType protocol,
    MeshProtocol fallback = MeshProtocol::Meshtastic);
const char* nodeProtocolName(contacts::NodeProtocolType protocol);
const char* nodeProtocolShortName(contacts::NodeProtocolType protocol);

} // namespace chat::infra
