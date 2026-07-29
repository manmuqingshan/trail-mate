/**
 * @file contacts_filter_profile.h
 * @brief Protocol-specific Contacts filter profile helpers.
 */

#pragma once

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "ui/screens/contacts/contacts_state.h"

namespace contacts
{
namespace ui
{

inline bool uses_reticulum_filter_profile()
{
    return chat::infra::isReticulumMeshProtocol(app::configFacade().readConfig().mesh_protocol);
}

inline bool uses_meshcore_filter_profile()
{
    return app::configFacade().readConfig().mesh_protocol == chat::MeshProtocol::MeshCore;
}

} // namespace ui
} // namespace contacts
