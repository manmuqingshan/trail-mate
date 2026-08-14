#include "platform/ui/a7682e_cellular_runtime.h"

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)

#include <Arduino.h>

#include "boards/tdeck_pro/board_profile.h"
#include "platform/ui/settings_store.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace platform::ui::a7682e
{
namespace
{

constexpr const char* kSettingsNamespace = "a7682e";
constexpr uint32_t kPowerLowMs = 10;
constexpr uint32_t kPowerHighMs = 50;
constexpr uint32_t kPowerBootWaitMs = 1200;
constexpr uint32_t kPowerOffHighMs = 3000;
constexpr uint32_t kAtTimeoutMs = 5000;
constexpr uint32_t kSmsTimeoutMs = 60000;
constexpr uint32_t kEmailTimeoutMs = 90000;
constexpr size_t kLineCapacity = 256;
constexpr size_t kSmsCapacity = 161;
constexpr size_t kEmailBodyCapacity = 512;

enum class PowerPhase : uint8_t
{
    Idle,
    StartLow,
    StartHigh,
    StartWait,
    StopHigh,
};

enum class CommandTag : uint8_t
{
    None,
    InitAt,
    InitEcho,
    InitErrors,
    InitClip,
    InitCnmi,
    InitMessageFormat,
    InitSmsCenter,
    InitSpeakerGain,
    InitMicrophoneGain,
    InitSim,
    InitSignal,
    InitRegistration,
    InitPacketRegistration,
    InitOperator,
    Dial,
    Answer,
    HangUp,
    SmsPrompt,
    SmsSubmit,
    EmailPdpContext,
    EmailPdpAuth,
    EmailActivate,
    EmailServer,
    EmailAuth,
    EmailFrom,
    EmailRecipient,
    EmailSubjectPrompt,
    EmailBodyPrompt,
    EmailSend,
};

struct SmsJob
{
    char recipient[40]{};
    char body[kSmsCapacity]{};
};

struct EmailJob
{
    char recipient[96]{};
    char subject[96]{};
    char body[kEmailBodyCapacity]{};
};

struct Runtime
{
    Config config{};
    Status status{};
    ServiceState service_state = ServiceState::Off;
    PowerPhase power_phase = PowerPhase::Idle;
    CommandTag command = CommandTag::None;
    uint32_t phase_started_at_ms = 0;
    uint32_t command_deadline_ms = 0;
    uint8_t init_step = 0;
    uint8_t email_step = 0;
    uint8_t at_failures = 0;
    bool loaded = false;
    bool expecting_sms_body = false;
    bool incoming_sms_header = false;
    bool email_send_reported = false;
    char line[kLineCapacity]{};
    char command_buffer[256]{};
    size_t line_length = 0;
    SmsJob sms{};
    EmailJob email{};
};

Runtime s_runtime;
HardwareSerial s_modem_serial(1);

bool elapsed(uint32_t now_ms, uint32_t started_at_ms, uint32_t duration_ms)
{
    return static_cast<uint32_t>(now_ms - started_at_ms) >= duration_ms;
}

bool valid_pin(int pin)
{
    return pin >= 0;
}

const boards::tdeck_pro::BoardProfile::OptionalModulePins& pins()
{
    return boards::tdeck_pro::kBoardProfile.optional;
}

void copy_text(char* destination, size_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0)
    {
        return;
    }
    const char* safe = source != nullptr ? source : "";
    std::snprintf(destination, capacity, "%s", safe);
}

bool reached_deadline(uint32_t now_ms, uint32_t deadline_ms)
{
    return deadline_ms != 0 && static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

bool has_text(const char* text)
{
    return text != nullptr && text[0] != '\0';
}

bool is_safe_quoted_parameter(const char* text)
{
    if (!has_text(text))
    {
        return false;
    }
    for (const char* current = text; *current != '\0'; ++current)
    {
        if (*current == '"' || *current == '\r' || *current == '\n' ||
            static_cast<unsigned char>(*current) == 0x1AU)
        {
            return false;
        }
    }
    return true;
}

bool is_safe_phone_number(const char* number)
{
    if (!has_text(number))
    {
        return false;
    }
    for (const char* current = number; *current != '\0'; ++current)
    {
        if (!std::isdigit(static_cast<unsigned char>(*current)) && *current != '+' &&
            *current != '*' && *current != '#')
        {
            return false;
        }
    }
    return true;
}

bool is_safe_payload(const char* text)
{
    if (!has_text(text))
    {
        return false;
    }
    for (const char* current = text; *current != '\0'; ++current)
    {
        if (static_cast<unsigned char>(*current) == 0x1AU)
        {
            return false;
        }
    }
    return true;
}

void set_event(const char* event)
{
    copy_text(s_runtime.status.last_event, sizeof(s_runtime.status.last_event), event);
}

void clear_pending_command()
{
    s_runtime.command = CommandTag::None;
    s_runtime.command_deadline_ms = 0;
    s_runtime.expecting_sms_body = false;
}

void send_command(const char* command, CommandTag tag, uint32_t timeout_ms = kAtTimeoutMs)
{
    if (command == nullptr || s_runtime.service_state == ServiceState::Off ||
        s_runtime.service_state == ServiceState::Stopping)
    {
        return;
    }

    s_modem_serial.print(command);
    s_modem_serial.print('\r');
    s_runtime.command = tag;
    s_runtime.command_deadline_ms = millis() + timeout_ms;
}

void modem_start_uart()
{
    // LilyGO's example intentionally passes the modem-labelled TXD/RXD values
    // in this order. Keep that wiring order rather than assuming UartPins are
    // MCU-direction labels.
    s_modem_serial.begin(pins().a7682e_uart.baud,
                         SERIAL_8N1,
                         pins().a7682e_uart.tx,
                         pins().a7682e_uart.rx);
    s_modem_serial.setTimeout(20);
}

void modem_stop_uart()
{
    s_modem_serial.flush();
    s_modem_serial.end();
}

void start_power_on()
{
    if (!s_runtime.config.enabled || !valid_pin(pins().a7682e_enable) ||
        !valid_pin(pins().a7682e_pwrkey))
    {
        return;
    }

    pinMode(pins().a7682e_enable, OUTPUT);
    pinMode(pins().a7682e_pwrkey, OUTPUT);
    digitalWrite(pins().a7682e_enable, HIGH);
    digitalWrite(pins().a7682e_pwrkey, LOW);
    modem_start_uart();
    s_runtime.status.enabled = true;
    s_runtime.status.powered = true;
    s_runtime.status.modem_ready = false;
    s_runtime.service_state = ServiceState::Starting;
    s_runtime.power_phase = PowerPhase::StartLow;
    s_runtime.phase_started_at_ms = millis();
    set_event("4G starting");
}

void ensure_powered_off()
{
    if (s_runtime.service_state != ServiceState::Off)
    {
        return;
    }
    if (valid_pin(pins().a7682e_pwrkey))
    {
        pinMode(pins().a7682e_pwrkey, OUTPUT);
        digitalWrite(pins().a7682e_pwrkey, LOW);
    }
    if (valid_pin(pins().a7682e_enable))
    {
        pinMode(pins().a7682e_enable, OUTPUT);
        digitalWrite(pins().a7682e_enable, LOW);
    }
    s_runtime.status.powered = false;
}

void begin_power_down()
{
    clear_pending_command();
    s_runtime.status.call_state = CallState::Idle;
    s_runtime.status.incoming_number[0] = '\0';
    s_runtime.status.modem_ready = false;
    s_runtime.status.network_registered = false;
    s_runtime.status.data_ready = false;
    s_runtime.status.powered = false;
    s_runtime.status.enabled = false;
    s_runtime.service_state = ServiceState::Stopping;

    if (!valid_pin(pins().a7682e_pwrkey))
    {
        modem_stop_uart();
        s_runtime.service_state = ServiceState::Off;
        return;
    }

    digitalWrite(pins().a7682e_pwrkey, LOW);
    s_runtime.power_phase = PowerPhase::StopHigh;
    s_runtime.phase_started_at_ms = millis();
    set_event("4G stopping");
}

void persist_string(const char* key, const char* value)
{
    (void)::platform::ui::settings_store::put_string(kSettingsNamespace, key, value != nullptr ? value : "");
}

void load_string(const char* key, char* output, size_t capacity)
{
    std::string stored;
    if (::platform::ui::settings_store::get_string(kSettingsNamespace, key, stored))
    {
        copy_text(output, capacity, stored.c_str());
    }
}

void load_config_once()
{
    if (s_runtime.loaded)
    {
        return;
    }
    s_runtime.loaded = true;
    s_runtime.status.supported = true;

    Config& config = s_runtime.config;
    config.enabled = ::platform::ui::settings_store::get_bool(kSettingsNamespace, "enabled", false);
    config.auto_answer = ::platform::ui::settings_store::get_bool(kSettingsNamespace, "auto_answer", false);
    config.speaker_gain = static_cast<uint8_t>(::platform::ui::settings_store::get_uint(
        kSettingsNamespace, "speaker_gain", config.speaker_gain));
    config.microphone_gain = static_cast<uint8_t>(::platform::ui::settings_store::get_uint(
        kSettingsNamespace, "microphone_gain", config.microphone_gain));
    config.smtp_port = static_cast<uint16_t>(::platform::ui::settings_store::get_uint(
        kSettingsNamespace, "smtp_port", config.smtp_port));
    config.smtp_security = static_cast<uint8_t>(::platform::ui::settings_store::get_uint(
        kSettingsNamespace, "smtp_security", config.smtp_security));
    load_string("apn", config.apn, sizeof(config.apn));
    load_string("apn_user", config.apn_user, sizeof(config.apn_user));
    load_string("apn_password", config.apn_password, sizeof(config.apn_password));
    load_string("smsc", config.smsc, sizeof(config.smsc));
    load_string("smtp_host", config.smtp_host, sizeof(config.smtp_host));
    load_string("smtp_user", config.smtp_user, sizeof(config.smtp_user));
    load_string("smtp_password", config.smtp_password, sizeof(config.smtp_password));
    load_string("smtp_from", config.smtp_from, sizeof(config.smtp_from));
    load_string("smtp_default_recipient",
                config.smtp_default_recipient,
                sizeof(config.smtp_default_recipient));
    s_runtime.status.enabled = config.enabled;
}

void finish_email(bool success)
{
    clear_pending_command();
    if (success)
    {
        s_runtime.status.data_ready = true;
        s_runtime.status.smtps_available = true;
        set_event("Email sent");
    }
    else
    {
        set_event("Email failed");
    }
    s_runtime.email = EmailJob{};
    s_runtime.email_send_reported = false;
}

void start_next_initialization_command();
void start_next_email_command();

void command_succeeded()
{
    const CommandTag completed = s_runtime.command;
    clear_pending_command();

    switch (completed)
    {
    case CommandTag::InitAt:
    case CommandTag::InitEcho:
    case CommandTag::InitErrors:
    case CommandTag::InitClip:
    case CommandTag::InitCnmi:
    case CommandTag::InitMessageFormat:
    case CommandTag::InitSmsCenter:
    case CommandTag::InitSpeakerGain:
    case CommandTag::InitMicrophoneGain:
    case CommandTag::InitSim:
    case CommandTag::InitSignal:
    case CommandTag::InitRegistration:
    case CommandTag::InitPacketRegistration:
    case CommandTag::InitOperator:
        ++s_runtime.init_step;
        start_next_initialization_command();
        break;
    case CommandTag::Dial:
        s_runtime.status.call_state = CallState::Dialing;
        set_event("Dialing");
        break;
    case CommandTag::Answer:
        s_runtime.status.call_state = CallState::Active;
        s_runtime.status.incoming_number[0] = '\0';
        set_event("Call active");
        break;
    case CommandTag::HangUp:
        s_runtime.status.call_state = CallState::Idle;
        s_runtime.status.incoming_number[0] = '\0';
        set_event("Call ended");
        break;
    case CommandTag::SmsSubmit:
        set_event("SMS sent");
        s_runtime.sms = SmsJob{};
        break;
    case CommandTag::EmailPdpContext:
    case CommandTag::EmailPdpAuth:
    case CommandTag::EmailActivate:
    case CommandTag::EmailServer:
    case CommandTag::EmailAuth:
    case CommandTag::EmailFrom:
    case CommandTag::EmailRecipient:
    case CommandTag::EmailSubjectPrompt:
    case CommandTag::EmailBodyPrompt:
        ++s_runtime.email_step;
        start_next_email_command();
        break;
    case CommandTag::EmailSend:
        finish_email(s_runtime.email_send_reported);
        break;
    case CommandTag::None:
    case CommandTag::SmsPrompt:
        break;
    }
}

void command_failed(const char* reason)
{
    const CommandTag failed = s_runtime.command;
    clear_pending_command();

    if (failed == CommandTag::InitAt)
    {
        ++s_runtime.at_failures;
        if (s_runtime.at_failures < 4)
        {
            s_runtime.command_deadline_ms = millis() + 1000;
            return;
        }
        s_runtime.service_state = ServiceState::Fault;
        set_event("Modem not responding");
        return;
    }

    if (failed >= CommandTag::EmailPdpContext && failed <= CommandTag::EmailSend)
    {
        s_runtime.status.smtps_available = false;
        finish_email(false);
        return;
    }
    if (failed == CommandTag::SmsPrompt || failed == CommandTag::SmsSubmit)
    {
        s_runtime.sms = SmsJob{};
        set_event("SMS failed");
        return;
    }
    if (failed == CommandTag::Dial || failed == CommandTag::Answer || failed == CommandTag::HangUp)
    {
        set_event(reason != nullptr ? reason : "Call command failed");
        return;
    }

    ++s_runtime.init_step;
    start_next_initialization_command();
}

void parse_quoted_number(const char* line, char* destination, size_t capacity)
{
    if (line == nullptr || destination == nullptr || capacity == 0)
    {
        return;
    }
    const char* begin = std::strchr(line, '"');
    if (begin == nullptr)
    {
        return;
    }
    ++begin;
    const char* end = std::strchr(begin, '"');
    if (end == nullptr)
    {
        return;
    }
    const size_t count = static_cast<size_t>(end - begin);
    const size_t copy_count = count < capacity - 1 ? count : capacity - 1;
    std::memcpy(destination, begin, copy_count);
    destination[copy_count] = '\0';
}

void parse_signal(const char* line)
{
    const char* delimiter = std::strchr(line, ':');
    if (delimiter == nullptr)
    {
        return;
    }
    s_runtime.status.rssi = static_cast<int16_t>(std::atoi(delimiter + 1));
}

void parse_registration(const char* line, bool packet)
{
    const char* comma = std::strrchr(line, ',');
    if (comma == nullptr)
    {
        return;
    }
    const int registration = std::atoi(comma + 1);
    const bool registered = registration == 1 || registration == 5;
    if (packet)
    {
        s_runtime.status.data_ready = registered;
    }
    else
    {
        s_runtime.status.network_registered = registered;
    }
}

void handle_line(const char* line)
{
    if (line == nullptr || line[0] == '\0')
    {
        return;
    }

    if (s_runtime.incoming_sms_header)
    {
        copy_text(s_runtime.status.last_sms_body, sizeof(s_runtime.status.last_sms_body), line);
        ++s_runtime.status.received_sms_count;
        s_runtime.incoming_sms_header = false;
        set_event("SMS received");
        return;
    }

    if (std::strncmp(line, "+CMT:", 5) == 0)
    {
        parse_quoted_number(line,
                            s_runtime.status.last_sms_sender,
                            sizeof(s_runtime.status.last_sms_sender));
        s_runtime.incoming_sms_header = true;
        return;
    }

    if (std::strcmp(line, "RING") == 0)
    {
        s_runtime.status.call_state = CallState::Incoming;
        set_event("Incoming call");
        if (s_runtime.config.auto_answer && s_runtime.command == CommandTag::None)
        {
            (void)answer();
        }
        return;
    }
    if (std::strncmp(line, "+CLIP:", 6) == 0)
    {
        parse_quoted_number(line,
                            s_runtime.status.incoming_number,
                            sizeof(s_runtime.status.incoming_number));
        return;
    }
    if (std::strncmp(line, "+CSQ:", 5) == 0)
    {
        parse_signal(line);
        return;
    }
    if (std::strncmp(line, "+CREG:", 6) == 0)
    {
        parse_registration(line, false);
        return;
    }
    if (std::strncmp(line, "+CGREG:", 7) == 0 || std::strncmp(line, "+CEREG:", 7) == 0)
    {
        parse_registration(line, true);
        return;
    }
    if (std::strncmp(line, "+COPS:", 6) == 0)
    {
        parse_quoted_number(line, s_runtime.status.operator_name, sizeof(s_runtime.status.operator_name));
        return;
    }
    if (std::strncmp(line, "+CPIN:", 6) == 0)
    {
        s_runtime.status.sim_ready = std::strstr(line, "READY") != nullptr;
        return;
    }
    if (std::strncmp(line, "+CSMTPSSEND:", 12) == 0)
    {
        const bool success = std::strstr(line, ": 0") != nullptr ||
                             std::strstr(line, ":0") != nullptr;
        s_runtime.email_send_reported = success;
        if (s_runtime.command == CommandTag::EmailSend)
        {
            finish_email(success);
        }
        return;
    }
    if (std::strcmp(line, "CONNECT") == 0)
    {
        s_runtime.status.call_state = CallState::Active;
        set_event("Call active");
        return;
    }
    if (std::strcmp(line, "NO CARRIER") == 0 || std::strcmp(line, "BUSY") == 0 ||
        std::strcmp(line, "NO ANSWER") == 0 || std::strcmp(line, "VOICE CALL END") == 0)
    {
        s_runtime.status.call_state = CallState::Idle;
        s_runtime.status.incoming_number[0] = '\0';
        if (s_runtime.command != CommandTag::None)
        {
            command_failed(line);
        }
        else
        {
            set_event("Call ended");
        }
        return;
    }
    if (std::strcmp(line, "OK") == 0)
    {
        command_succeeded();
        return;
    }
    if (std::strcmp(line, "ERROR") == 0 || std::strncmp(line, "+CME ERROR", 10) == 0 ||
        std::strncmp(line, "+CMS ERROR", 10) == 0)
    {
        command_failed(line);
    }
}

void receive_modem_data()
{
    for (size_t count = 0; count < 96 && s_modem_serial.available() > 0; ++count)
    {
        const int input = s_modem_serial.read();
        if (input < 0)
        {
            return;
        }
        const char character = static_cast<char>(input);
        if (character == '>' && (s_runtime.command == CommandTag::SmsPrompt ||
                                 s_runtime.command == CommandTag::EmailSubjectPrompt ||
                                 s_runtime.command == CommandTag::EmailBodyPrompt))
        {
            if (s_runtime.command == CommandTag::SmsPrompt)
            {
                s_modem_serial.print(s_runtime.sms.body);
                s_modem_serial.write(static_cast<uint8_t>(0x1A));
                s_runtime.command = CommandTag::SmsSubmit;
                s_runtime.command_deadline_ms = millis() + kSmsTimeoutMs;
                set_event("Sending SMS");
            }
            else if (s_runtime.command == CommandTag::EmailSubjectPrompt)
            {
                s_modem_serial.print(s_runtime.email.subject);
                s_modem_serial.write(static_cast<uint8_t>(0x1A));
            }
            else
            {
                s_modem_serial.print(s_runtime.email.body);
                s_modem_serial.write(static_cast<uint8_t>(0x1A));
            }
            continue;
        }
        if (character == '\r')
        {
            continue;
        }
        if (character == '\n')
        {
            s_runtime.line[s_runtime.line_length] = '\0';
            handle_line(s_runtime.line);
            s_runtime.line_length = 0;
            continue;
        }
        if (s_runtime.line_length + 1 < sizeof(s_runtime.line))
        {
            s_runtime.line[s_runtime.line_length++] = character;
        }
    }
}

void start_next_initialization_command()
{
    if (s_runtime.service_state != ServiceState::Initializing || s_runtime.command != CommandTag::None)
    {
        return;
    }

    char* const command = s_runtime.command_buffer;
    const size_t command_capacity = sizeof(s_runtime.command_buffer);
    switch (s_runtime.init_step)
    {
    case 0:
        send_command("AT", CommandTag::InitAt);
        return;
    case 1:
        send_command("ATE0", CommandTag::InitEcho);
        return;
    case 2:
        send_command("AT+CMEE=2", CommandTag::InitErrors);
        return;
    case 3:
        send_command("AT+CLIP=1", CommandTag::InitClip);
        return;
    case 4:
        send_command("AT+CNMI=2,2,0,0,0", CommandTag::InitCnmi);
        return;
    case 5:
        send_command("AT+CMGF=1", CommandTag::InitMessageFormat);
        return;
    case 6:
        if (has_text(s_runtime.config.smsc) && is_safe_quoted_parameter(s_runtime.config.smsc))
        {
            std::snprintf(command, command_capacity, "AT+CSCA=\"%s\"", s_runtime.config.smsc);
            send_command(command, CommandTag::InitSmsCenter);
            return;
        }
        ++s_runtime.init_step;
        start_next_initialization_command();
        return;
    case 7:
        std::snprintf(command,
                      command_capacity,
                      "AT+COUTGAIN=%u",
                      static_cast<unsigned>(s_runtime.config.speaker_gain));
        send_command(command, CommandTag::InitSpeakerGain);
        return;
    case 8:
        std::snprintf(command,
                      command_capacity,
                      "AT+CMICGAIN=0,%u",
                      static_cast<unsigned>(s_runtime.config.microphone_gain));
        send_command(command, CommandTag::InitMicrophoneGain);
        return;
    case 9:
        send_command("AT+CPIN?", CommandTag::InitSim);
        return;
    case 10:
        send_command("AT+CSQ", CommandTag::InitSignal);
        return;
    case 11:
        send_command("AT+CREG?", CommandTag::InitRegistration);
        return;
    case 12:
        send_command("AT+CGREG?", CommandTag::InitPacketRegistration);
        return;
    case 13:
        send_command("AT+COPS?", CommandTag::InitOperator, 15000);
        return;
    default:
        s_runtime.status.modem_ready = true;
        s_runtime.service_state = ServiceState::Ready;
        set_event("4G ready");
        return;
    }
}

void start_next_email_command()
{
    if (s_runtime.command != CommandTag::None || !has_text(s_runtime.email.recipient))
    {
        return;
    }

    char* const command = s_runtime.command_buffer;
    const size_t command_capacity = sizeof(s_runtime.command_buffer);
    switch (s_runtime.email_step)
    {
    case 0:
        if (has_text(s_runtime.config.apn))
        {
            std::snprintf(command, command_capacity, "AT+CGDCONT=1,\"IP\",\"%s\"", s_runtime.config.apn);
            send_command(command, CommandTag::EmailPdpContext, 15000);
            return;
        }
        s_runtime.email_step = 1;
        start_next_email_command();
        return;
    case 1:
        if (has_text(s_runtime.config.apn_user))
        {
            std::snprintf(command,
                          command_capacity,
                          "AT+CGAUTH=1,1,\"%s\",\"%s\"",
                          s_runtime.config.apn_user,
                          s_runtime.config.apn_password);
            send_command(command, CommandTag::EmailPdpAuth, 15000);
            return;
        }
        s_runtime.email_step = 2;
        start_next_email_command();
        return;
    case 2:
        send_command("AT+CGACT=1,1", CommandTag::EmailActivate, 30000);
        return;
    case 3:
        std::snprintf(command,
                      command_capacity,
                      "AT+CSMTPSSRV=\"%s\",%u,%u",
                      s_runtime.config.smtp_host,
                      static_cast<unsigned>(s_runtime.config.smtp_port),
                      static_cast<unsigned>(s_runtime.config.smtp_security));
        send_command(command, CommandTag::EmailServer, 15000);
        return;
    case 4:
        std::snprintf(command,
                      command_capacity,
                      "AT+CSMTPSAUTH=1,\"%s\",\"%s\"",
                      s_runtime.config.smtp_user,
                      s_runtime.config.smtp_password);
        send_command(command, CommandTag::EmailAuth, 15000);
        return;
    case 5:
        std::snprintf(command,
                      command_capacity,
                      "AT+CSMTPSFROM=\"%s\",\"Trail Mate\"",
                      s_runtime.config.smtp_from);
        send_command(command, CommandTag::EmailFrom, 15000);
        return;
    case 6:
        std::snprintf(command,
                      command_capacity,
                      "AT+CSMTPSRCPT=0,0,\"%s\",\"%s\"",
                      s_runtime.email.recipient,
                      s_runtime.email.recipient);
        send_command(command, CommandTag::EmailRecipient, 15000);
        return;
    case 7:
        send_command("AT+CSMTPSSUB=2", CommandTag::EmailSubjectPrompt, 15000);
        return;
    case 8:
        send_command("AT+CSMTPSBODY=2", CommandTag::EmailBodyPrompt, 15000);
        return;
    case 9:
        s_runtime.email_send_reported = false;
        send_command("AT+CSMTPSSEND", CommandTag::EmailSend, kEmailTimeoutMs);
        set_event("Sending email");
        return;
    default:
        finish_email(false);
        return;
    }
}

void service_power(uint32_t now_ms)
{
    switch (s_runtime.power_phase)
    {
    case PowerPhase::StartLow:
        if (elapsed(now_ms, s_runtime.phase_started_at_ms, kPowerLowMs))
        {
            digitalWrite(pins().a7682e_pwrkey, HIGH);
            s_runtime.power_phase = PowerPhase::StartHigh;
            s_runtime.phase_started_at_ms = now_ms;
        }
        break;
    case PowerPhase::StartHigh:
        if (elapsed(now_ms, s_runtime.phase_started_at_ms, kPowerHighMs))
        {
            digitalWrite(pins().a7682e_pwrkey, LOW);
            s_runtime.power_phase = PowerPhase::StartWait;
            s_runtime.phase_started_at_ms = now_ms;
        }
        break;
    case PowerPhase::StartWait:
        if (elapsed(now_ms, s_runtime.phase_started_at_ms, kPowerBootWaitMs))
        {
            s_runtime.power_phase = PowerPhase::Idle;
            s_runtime.service_state = ServiceState::Initializing;
            s_runtime.init_step = 0;
            s_runtime.at_failures = 0;
            start_next_initialization_command();
        }
        break;
    case PowerPhase::StopHigh:
        if (elapsed(now_ms, s_runtime.phase_started_at_ms, kPowerLowMs))
        {
            digitalWrite(pins().a7682e_pwrkey, HIGH);
            s_runtime.phase_started_at_ms = now_ms;
            s_runtime.power_phase = PowerPhase::Idle;
        }
        break;
    case PowerPhase::Idle:
        if (s_runtime.service_state == ServiceState::Stopping &&
            elapsed(now_ms, s_runtime.phase_started_at_ms, kPowerOffHighMs))
        {
            digitalWrite(pins().a7682e_pwrkey, LOW);
            if (valid_pin(pins().a7682e_enable))
            {
                digitalWrite(pins().a7682e_enable, LOW);
            }
            modem_stop_uart();
            s_runtime.service_state = ServiceState::Off;
            set_event("4G off");
        }
        break;
    }
}

} // namespace

bool is_supported()
{
    return true;
}

const Config& config()
{
    load_config_once();
    return s_runtime.config;
}

const Status& status()
{
    load_config_once();
    return s_runtime.status;
}

bool save_config(const Config& next)
{
    load_config_once();
    s_runtime.config = next;
    ::platform::ui::settings_store::put_bool(kSettingsNamespace, "enabled", next.enabled);
    ::platform::ui::settings_store::put_bool(kSettingsNamespace, "auto_answer", next.auto_answer);
    ::platform::ui::settings_store::put_uint(kSettingsNamespace, "speaker_gain", next.speaker_gain);
    ::platform::ui::settings_store::put_uint(kSettingsNamespace, "microphone_gain", next.microphone_gain);
    ::platform::ui::settings_store::put_uint(kSettingsNamespace, "smtp_port", next.smtp_port);
    ::platform::ui::settings_store::put_uint(kSettingsNamespace, "smtp_security", next.smtp_security);
    persist_string("apn", next.apn);
    persist_string("apn_user", next.apn_user);
    persist_string("apn_password", next.apn_password);
    persist_string("smsc", next.smsc);
    persist_string("smtp_host", next.smtp_host);
    persist_string("smtp_user", next.smtp_user);
    persist_string("smtp_password", next.smtp_password);
    persist_string("smtp_from", next.smtp_from);
    persist_string("smtp_default_recipient", next.smtp_default_recipient);
    (void)set_enabled(next.enabled);
    return true;
}

bool set_enabled(bool enabled)
{
    load_config_once();
    if (s_runtime.config.enabled == enabled)
    {
        if (enabled && s_runtime.service_state != ServiceState::Off)
        {
            return true;
        }
        if (!enabled && s_runtime.service_state == ServiceState::Off)
        {
            ensure_powered_off();
            return true;
        }
        if (!enabled && s_runtime.service_state == ServiceState::Stopping)
        {
            return true;
        }
    }
    s_runtime.config.enabled = enabled;
    s_runtime.status.enabled = enabled;
    ::platform::ui::settings_store::put_bool(kSettingsNamespace, "enabled", enabled);
    if (enabled)
    {
        start_power_on();
    }
    else if (s_runtime.service_state == ServiceState::Off)
    {
        ensure_powered_off();
    }
    else
    {
        begin_power_down();
    }
    return true;
}

bool dial(const char* number)
{
    if (!is_safe_phone_number(number) || s_runtime.service_state != ServiceState::Ready ||
        s_runtime.command != CommandTag::None)
    {
        set_event("Dial unavailable");
        return false;
    }
    std::snprintf(s_runtime.command_buffer,
                  sizeof(s_runtime.command_buffer),
                  "ATD%s;",
                  number);
    send_command(s_runtime.command_buffer, CommandTag::Dial, 15000);
    return true;
}

bool answer()
{
    if (s_runtime.status.call_state != CallState::Incoming ||
        s_runtime.command != CommandTag::None || s_runtime.service_state != ServiceState::Ready)
    {
        set_event("No incoming call");
        return false;
    }
    send_command("ATA", CommandTag::Answer, 15000);
    return true;
}

bool hang_up()
{
    if (s_runtime.status.call_state == CallState::Idle || s_runtime.command != CommandTag::None ||
        s_runtime.service_state != ServiceState::Ready)
    {
        set_event("No active call");
        return false;
    }
    send_command("AT+CHUP", CommandTag::HangUp, 15000);
    return true;
}

bool send_sms(const char* recipient, const char* body)
{
    if (!is_safe_phone_number(recipient) || !is_safe_payload(body) ||
        s_runtime.service_state != ServiceState::Ready || s_runtime.command != CommandTag::None)
    {
        set_event("SMS unavailable");
        return false;
    }
    copy_text(s_runtime.sms.recipient, sizeof(s_runtime.sms.recipient), recipient);
    copy_text(s_runtime.sms.body, sizeof(s_runtime.sms.body), body);
    std::snprintf(s_runtime.command_buffer,
                  sizeof(s_runtime.command_buffer),
                  "AT+CMGS=\"%s\"",
                  s_runtime.sms.recipient);
    send_command(s_runtime.command_buffer, CommandTag::SmsPrompt, 15000);
    return true;
}

bool send_email(const char* recipient, const char* subject, const char* body)
{
    const Config& current = config();
    const char* target = has_text(recipient) ? recipient : current.smtp_default_recipient;
    if (!is_safe_quoted_parameter(target) || !is_safe_payload(subject) || !is_safe_payload(body) ||
        (has_text(current.apn) && !is_safe_quoted_parameter(current.apn)) ||
        (has_text(current.apn_user) && !is_safe_quoted_parameter(current.apn_user)) ||
        (has_text(current.apn_password) && !is_safe_quoted_parameter(current.apn_password)) ||
        !is_safe_quoted_parameter(current.smtp_host) || !is_safe_quoted_parameter(current.smtp_user) ||
        !is_safe_quoted_parameter(current.smtp_password) || !is_safe_quoted_parameter(current.smtp_from) ||
        s_runtime.service_state != ServiceState::Ready || s_runtime.command != CommandTag::None)
    {
        set_event("Email configuration incomplete");
        return false;
    }
    copy_text(s_runtime.email.recipient, sizeof(s_runtime.email.recipient), target);
    copy_text(s_runtime.email.subject, sizeof(s_runtime.email.subject), subject);
    copy_text(s_runtime.email.body, sizeof(s_runtime.email.body), body);
    s_runtime.email_step = 0;
    start_next_email_command();
    return true;
}

void tick()
{
    load_config_once();
    const uint32_t now_ms = millis();

    if (s_runtime.config.enabled && s_runtime.service_state == ServiceState::Off)
    {
        start_power_on();
    }
    else if (!s_runtime.config.enabled && s_runtime.service_state == ServiceState::Off)
    {
        ensure_powered_off();
    }
    service_power(now_ms);
    if (s_runtime.service_state == ServiceState::Off || s_runtime.service_state == ServiceState::Stopping)
    {
        return;
    }

    receive_modem_data();
    if (s_runtime.command != CommandTag::None && reached_deadline(now_ms, s_runtime.command_deadline_ms))
    {
        command_failed("AT timeout");
    }
    if (s_runtime.service_state == ServiceState::Initializing && s_runtime.command == CommandTag::None &&
        s_runtime.command_deadline_ms != 0 && reached_deadline(now_ms, s_runtime.command_deadline_ms))
    {
        start_next_initialization_command();
    }
}

ServiceState service_state()
{
    return s_runtime.service_state;
}

const char* service_state_label(ServiceState state)
{
    switch (state)
    {
    case ServiceState::Off:
        return "OFF";
    case ServiceState::Starting:
        return "STARTING";
    case ServiceState::Initializing:
        return "INITIALIZING";
    case ServiceState::Ready:
        return "READY";
    case ServiceState::Stopping:
        return "STOPPING";
    case ServiceState::Fault:
        return "FAULT";
    }
    return "UNKNOWN";
}

const char* call_state_label(CallState state)
{
    switch (state)
    {
    case CallState::Idle:
        return "IDLE";
    case CallState::Dialing:
        return "DIALING";
    case CallState::Incoming:
        return "INCOMING";
    case CallState::Active:
        return "ACTIVE";
    }
    return "UNKNOWN";
}

} // namespace platform::ui::a7682e

#endif
