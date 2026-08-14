#include "cellular_page_internal.h"

#include "ui/app_runtime.h"

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{
namespace
{

bool saveSettings()
{
    State& current = state();
    storeFieldValues();
    if (current.port == nullptr || !current.port->saveSettings(current.settings))
    {
        setNotice("SETTINGS SAVE FAILED");
        return false;
    }
    setNotice("SETTINGS SAVED");
    return true;
}

bool hasPort()
{
    if (state().port != nullptr)
    {
        return true;
    }
    setNotice("CELLULAR UNAVAILABLE");
    return false;
}

} // namespace

void runAction(ButtonAction action)
{
    State& current = state();
    storeFieldValues();
    switch (action)
    {
    case ButtonAction::Dial:
        if (hasPort())
        {
            setNotice(current.port->dial(current.phone_number) ? "DIAL REQUESTED" : "DIAL REJECTED");
        }
        break;
    case ButtonAction::Answer:
        if (hasPort())
        {
            setNotice(current.port->answer() ? "ANSWER REQUESTED" : "NO INCOMING CALL");
        }
        break;
    case ButtonAction::HangUp:
        if (hasPort())
        {
            setNotice(current.port->hangUp() ? "HANGUP REQUESTED" : "NO ACTIVE CALL");
        }
        break;
    case ButtonAction::GoPhone:
        navigateTo(Page::Phone);
        return;
    case ButtonAction::GoSms:
        navigateTo(Page::Sms);
        return;
    case ButtonAction::GoEmail:
        navigateTo(Page::Email);
        return;
    case ButtonAction::GoRadioSettings:
        navigateTo(Page::RadioSettings);
        return;
    case ButtonAction::GoMailSettings:
        navigateTo(Page::MailSettings);
        return;
    case ButtonAction::SaveSettings:
        (void)saveSettings();
        break;
    case ButtonAction::ToggleCellular:
        current.settings.enabled = !current.settings.enabled;
        (void)saveSettings();
        scheduleRebuild(current.page);
        return;
    case ButtonAction::ToggleAutoAnswer:
        current.settings.auto_answer = !current.settings.auto_answer;
        (void)saveSettings();
        scheduleRebuild(current.page);
        return;
    case ButtonAction::CycleAudioGain:
        current.settings.speaker_gain = static_cast<uint8_t>((current.settings.speaker_gain + 1U) % 16U);
        current.settings.microphone_gain =
            static_cast<uint8_t>((current.settings.microphone_gain + 1U) % 16U);
        (void)saveSettings();
        scheduleRebuild(current.page);
        return;
    case ButtonAction::CycleSmtpPort:
        if (current.settings.smtp_port == 465)
        {
            current.settings.smtp_port = 587;
        }
        else if (current.settings.smtp_port == 587)
        {
            current.settings.smtp_port = 25;
        }
        else
        {
            current.settings.smtp_port = 465;
        }
        (void)saveSettings();
        scheduleRebuild(current.page);
        return;
    case ButtonAction::CycleSmtpSecurity:
        current.settings.smtp_security = static_cast<uint8_t>((current.settings.smtp_security + 1U) % 3U);
        (void)saveSettings();
        scheduleRebuild(current.page);
        return;
    case ButtonAction::SendSms:
        if (hasPort())
        {
            setNotice(current.port->sendSms(current.sms_recipient, current.sms_body) ? "SMS QUEUED" : "SMS REJECTED");
        }
        break;
    case ButtonAction::SendEmail:
        if (hasPort())
        {
            setNotice(current.port->sendEmail(current.email_recipient, current.email_subject, current.email_body)
                          ? "EMAIL QUEUED"
                          : "EMAIL REJECTED");
        }
        break;
    case ButtonAction::Back:
        if (!navigateBack())
        {
            ui_request_exit_to_menu();
        }
        return;
    }
    renderStatus();
}

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
