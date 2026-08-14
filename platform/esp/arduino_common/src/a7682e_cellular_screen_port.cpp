#include "platform/ui/a7682e_cellular_screen_port.h"

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)

#include "platform/ui/a7682e_cellular_runtime.h"
#include "ui/mono/screens/screen_240x320/cellular_port.h"

#include <cstdio>

namespace platform::ui::a7682e
{
namespace
{

using ::ui::mono::screens::screen_240x320::CellularCallState;
using ::ui::mono::screens::screen_240x320::CellularPort;
using ::ui::mono::screens::screen_240x320::CellularSettings;
using ::ui::mono::screens::screen_240x320::CellularStatus;

template <size_t N>
void copy_text(char (&destination)[N], const char* source)
{
    std::snprintf(destination, N, "%s", source != nullptr ? source : "");
}

CellularCallState to_screen_call_state(CallState state)
{
    switch (state)
    {
    case CallState::Idle:
        return CellularCallState::Idle;
    case CallState::Dialing:
        return CellularCallState::Dialing;
    case CallState::Incoming:
        return CellularCallState::Incoming;
    case CallState::Active:
        return CellularCallState::Active;
    }
    return CellularCallState::Idle;
}

class A7682eCellularScreenPort final : public CellularPort
{
  public:
    void readSettings(CellularSettings& out) const override
    {
        const Config& source = config();
        out.enabled = source.enabled;
        out.auto_answer = source.auto_answer;
        out.speaker_gain = source.speaker_gain;
        out.microphone_gain = source.microphone_gain;
        out.smtp_port = source.smtp_port;
        out.smtp_security = source.smtp_security;
        copy_text(out.apn, source.apn);
        copy_text(out.apn_user, source.apn_user);
        copy_text(out.apn_password, source.apn_password);
        copy_text(out.smsc, source.smsc);
        copy_text(out.smtp_host, source.smtp_host);
        copy_text(out.smtp_user, source.smtp_user);
        copy_text(out.smtp_password, source.smtp_password);
        copy_text(out.smtp_from, source.smtp_from);
        copy_text(out.smtp_default_recipient, source.smtp_default_recipient);
    }

    void readStatus(CellularStatus& out) const override
    {
        const Status& source = status();
        out.supported = source.supported;
        out.enabled = source.enabled;
        out.powered = source.powered;
        out.modem_ready = source.modem_ready;
        out.sim_ready = source.sim_ready;
        out.network_registered = source.network_registered;
        out.data_ready = source.data_ready;
        out.smtps_available = source.smtps_available;
        out.rssi = source.rssi;
        out.call_state = to_screen_call_state(source.call_state);
        out.received_sms_count = source.received_sms_count;
        copy_text(out.service_state, service_state_label(service_state()));
        copy_text(out.operator_name, source.operator_name);
        copy_text(out.incoming_number, source.incoming_number);
        copy_text(out.last_sms_sender, source.last_sms_sender);
        copy_text(out.last_sms_body, source.last_sms_body);
        copy_text(out.last_event, source.last_event);
    }

    bool saveSettings(const CellularSettings& settings) override
    {
        // This is static storage, not an ESP UI-task stack object.  It keeps
        // the capability boundary safe even though a full settings payload
        // contains APN and SMTP credentials.
        Config& target = settings_scratch_;
        target.enabled = settings.enabled;
        target.auto_answer = settings.auto_answer;
        target.speaker_gain = settings.speaker_gain;
        target.microphone_gain = settings.microphone_gain;
        target.smtp_port = settings.smtp_port;
        target.smtp_security = settings.smtp_security;
        copy_text(target.apn, settings.apn);
        copy_text(target.apn_user, settings.apn_user);
        copy_text(target.apn_password, settings.apn_password);
        copy_text(target.smsc, settings.smsc);
        copy_text(target.smtp_host, settings.smtp_host);
        copy_text(target.smtp_user, settings.smtp_user);
        copy_text(target.smtp_password, settings.smtp_password);
        copy_text(target.smtp_from, settings.smtp_from);
        copy_text(target.smtp_default_recipient, settings.smtp_default_recipient);
        return save_config(target);
    }

    bool dial(const char* number) override { return ::platform::ui::a7682e::dial(number); }
    bool answer() override { return ::platform::ui::a7682e::answer(); }
    bool hangUp() override { return ::platform::ui::a7682e::hang_up(); }
    bool sendSms(const char* recipient, const char* body) override
    {
        return ::platform::ui::a7682e::send_sms(recipient, body);
    }
    bool sendEmail(const char* recipient, const char* subject, const char* body) override
    {
        return ::platform::ui::a7682e::send_email(recipient, subject, body);
    }

  private:
    Config settings_scratch_{};
};

A7682eCellularScreenPort s_screen_port;

} // namespace

void install_screen_240x320_port()
{
    ::ui::mono::screens::screen_240x320::installCellularPort(&s_screen_port);
}

} // namespace platform::ui::a7682e

#else

namespace platform::ui::a7682e
{

void install_screen_240x320_port() {}

} // namespace platform::ui::a7682e

#endif
