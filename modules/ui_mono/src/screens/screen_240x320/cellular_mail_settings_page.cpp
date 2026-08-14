#include "cellular_page_internal.h"

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{

void buildMailSettingsPage()
{
    State& current = state();
    addField("HOST", current.settings.smtp_host, sizeof(current.settings.smtp_host), 132, 95);
    addField("USER", current.settings.smtp_user, sizeof(current.settings.smtp_user), 154, 95);
    addField("PASS", current.settings.smtp_password, sizeof(current.settings.smtp_password), 176, 95, true);
    addField("FROM", current.settings.smtp_from, sizeof(current.settings.smtp_from), 198, 95);
    addField("DEFAULT TO", current.settings.smtp_default_recipient, sizeof(current.settings.smtp_default_recipient), 220, 95);
    addButton("SAVE", ButtonAction::SaveSettings, 8, 248, 58);
    addButton("PORT", ButtonAction::CycleSmtpPort, 72, 248, 52);
    addButton("TLS", ButtonAction::CycleSmtpSecurity, 130, 248, 48);
    addButton("RADIO", ButtonAction::GoRadioSettings, 184, 248, 48);
    addButton("EMAIL", ButtonAction::GoEmail, 72, 270, 80);
    addButton("BACK", ButtonAction::Back, 8, 298, 58);
}

void renderMailSettingsStatus()
{
    State& current = state();
    setLine(0,
            "SMTP TLS:%u PORT:%u",
            static_cast<unsigned>(current.settings.smtp_security),
            static_cast<unsigned>(current.settings.smtp_port));
    setLine(1, "FROM:%s", current.settings.smtp_from);
    setLine(2, "TO:%s", current.settings.smtp_default_recipient);
    setLine(3, "Use an app password, not account password");
}

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
