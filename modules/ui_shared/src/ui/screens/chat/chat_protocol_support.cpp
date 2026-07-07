#include "ui/screens/chat/chat_protocol_support.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"

namespace chat::ui::support
{

chat::MeshProtocol active_mesh_protocol()
{
    return app::configFacade().getConfig().mesh_protocol;
}

chat::MeshCapabilities active_mesh_capabilities()
{
    chat::IMeshAdapter* adapter = app::messagingFacade().getMeshAdapter();
    return adapter ? adapter->getCapabilities() : chat::MeshCapabilities{};
}

bool supports_local_text_chat()
{
    return active_mesh_capabilities().supports_unicast_text;
}

bool supports_reticulum_destination_text()
{
    return active_mesh_capabilities().supports_reticulum_destination_text;
}

bool supports_team_chat()
{
    return active_mesh_capabilities().supports_unicast_appdata;
}

const char* local_text_chat_unavailable_message()
{
    return "Text chat unavailable";
}

const char* reticulum_destination_text_unavailable_message()
{
    return "Reticulum group chat unavailable";
}

const char* team_chat_unavailable_message()
{
    return "Team chat unavailable";
}

} // namespace chat::ui::support
