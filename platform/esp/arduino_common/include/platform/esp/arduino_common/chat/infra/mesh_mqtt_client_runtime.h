#pragma once

namespace app
{
class IAppFacade;
} // namespace app

namespace platform::esp::arduino_common::mesh_mqtt
{

bool wantsStandaloneMode(app::IAppFacade& app_context);
void update(app::IAppFacade& app_context);

} // namespace platform::esp::arduino_common::mesh_mqtt
