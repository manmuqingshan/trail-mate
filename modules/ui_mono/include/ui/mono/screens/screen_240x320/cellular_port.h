#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::mono::screens::screen_240x320
{

// This contract deliberately describes a cellular capability rather than an
// A7682E module.  A board/runtime adapter owns modem AT commands, power rails,
// persistence and credential storage; a 240x320 page only consumes this
// projection and emits user intents through it.
enum class CellularCallState : uint8_t
{
    Idle,
    Dialing,
    Incoming,
    Active,
};

struct CellularSettings
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

struct CellularStatus
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
    CellularCallState call_state = CellularCallState::Idle;
    uint32_t received_sms_count = 0;
    char service_state[16]{};
    char operator_name[40]{};
    char incoming_number[40]{};
    char last_sms_sender[40]{};
    char last_sms_body[192]{};
    char last_event[80]{};
};

class CellularPort
{
  public:
    virtual ~CellularPort() = default;

    virtual void readSettings(CellularSettings& out) const = 0;
    virtual void readStatus(CellularStatus& out) const = 0;
    virtual bool saveSettings(const CellularSettings& settings) = 0;
    virtual bool dial(const char* number) = 0;
    virtual bool answer() = 0;
    virtual bool hangUp() = 0;
    virtual bool sendSms(const char* recipient, const char* body) = 0;
    virtual bool sendEmail(const char* recipient, const char* subject, const char* body) = 0;
};

// The runtime installs its capability adapter during platform startup.  A
// missing port is a supported state: the generic screen remains present but
// reports that cellular capability is unavailable.
void installCellularPort(CellularPort* port);
CellularPort* cellularPort();

const char* cellularCallStateLabel(CellularCallState state);

} // namespace ui::mono::screens::screen_240x320
