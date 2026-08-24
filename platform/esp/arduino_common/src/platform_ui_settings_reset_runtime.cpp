#include "platform/ui/settings_reset_runtime.h"

#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"

namespace platform::ui::settings_reset
{

bool begin()
{
    ::app::sd_tms::beginWorkingConfigReset();
    if (::app::sd_tms::resetWorkingConfig())
    {
        return true;
    }

    ::app::sd_tms::endWorkingConfigReset();
    return false;
}

void finish()
{
    ::app::sd_tms::endWorkingConfigReset();
}

} // namespace platform::ui::settings_reset
