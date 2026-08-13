#pragma once

#include <cstdint>

namespace platform::ui::a7682e
{

enum class ServiceState : uint8_t
{
    Off,
    Starting,
    Initializing,
    Ready,
    Stopping,
    Fault,
};

enum class CallState : uint8_t
{
    Idle,
    Dialing,
    Incoming,
    Active,
};

// All strings are fixed-size so callers can keep configuration outside an ESP
// task stack. Credentials are persisted only in the device settings store.
struct Config
{
    bool enabled = false;
    bool auto_answer = false;
    uint8_t speaker_gain = 5;
    uint8_t microphone_gain = 5;
    uint16_t smtp_port = 465;
    uint8_t smtp_security = 2;
    char apn[64]{};
    char apn_user[48]{};
    char apn_password[64]{};
    char smsc[32]{};
    char smtp_host[96]{};
    char smtp_user[96]{};
    char smtp_password[96]{};
    char smtp_from[96]{};
    char smtp_default_recipient[96]{};
};

struct Status
{
    bool supported = false;
    bool enabled = false;
    bool powered = false;
    bool modem_ready = false;
    bool sim_ready = false;
    bool network_registered = false;
    bool data_ready = false;
    bool smtps_available = false;
    int16_t rssi = -1;
    CallState call_state = CallState::Idle;
    uint32_t received_sms_count = 0;
    char operator_name[40]{};
    char incoming_number[40]{};
    char last_sms_sender[40]{};
    char last_sms_body[192]{};
    char last_event[80]{};
};

// The service is intentionally tick-driven: it shares the existing Arduino
// application loop and never creates a second task or blocks the UI.
void tick();

bool is_supported();
const Config& config();
const Status& status();

bool save_config(const Config& next);
bool set_enabled(bool enabled);

bool dial(const char* number);
bool answer();
bool hang_up();
bool send_sms(const char* recipient, const char* body);
bool send_email(const char* recipient, const char* subject, const char* body);

const char* service_state_label(ServiceState state);
ServiceState service_state();
const char* call_state_label(CallState state);

} // namespace platform::ui::a7682e
