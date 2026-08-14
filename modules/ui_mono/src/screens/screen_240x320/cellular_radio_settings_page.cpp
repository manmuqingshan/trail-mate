#include "cellular_page_internal.h"

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{

void buildRadioSettingsPage()
{
    State& current = state();
    addField("APN", current.settings.apn, sizeof(current.settings.apn), 132, 63);
    addField("APN USER", current.settings.apn_user, sizeof(current.settings.apn_user), 154, 47);
    addField("APN PASS", current.settings.apn_password, sizeof(current.settings.apn_password), 176, 63, true);
    addField("SMSC", current.settings.smsc, sizeof(current.settings.smsc), 198, 31, false, "+0123456789");
    addButton(current.settings.enabled ? "CELL OFF" : "CELL ON", ButtonAction::ToggleCellular, 8, 226, 70);
    addButton(current.settings.auto_answer ? "AUTO ON" : "AUTO OFF", ButtonAction::ToggleAutoAnswer, 84, 226, 76);
    addButton("GAIN", ButtonAction::CycleAudioGain, 166, 226, 66);
    addButton("SAVE", ButtonAction::SaveSettings, 8, 250, 58);
    addButton("MAIL", ButtonAction::GoMailSettings, 72, 250, 84);
    addButton("BACK", ButtonAction::Back, 8, 298, 58);
}

void renderRadioSettingsStatus()
{
    State& current = state();
    setLine(0,
            "CELL:%s AUTO:%s",
            current.settings.enabled ? "ON" : "OFF",
            current.settings.auto_answer ? "ON" : "OFF");
    setLine(1,
            "SPEAKER:%u MIC:%u",
            static_cast<unsigned>(current.settings.speaker_gain),
            static_cast<unsigned>(current.settings.microphone_gain));
    setLine(2, "SMSC:%s", current.settings.smsc[0] != '\0' ? current.settings.smsc : "AUTO");
    setLine(3, "APN password is stored locally");
}

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
