#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_conversation.cpp
 * @brief Chat conversation screen implementation
 */
#include "ui/screens/chat/chat_conversation_components.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/usecase/contact_service.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/chat/chat_conversation_input.h"
#include "ui/screens/chat/chat_conversation_layout.h"
#include "ui/screens/chat/chat_conversation_styles.h"
#include "ui/ui_common.h"
#include "ui_presentation/chat/chat_workspace_snapshot.h"

#include "sys/clock.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifndef CHAT_CONVERSATION_LOG_ENABLE
#define CHAT_CONVERSATION_LOG_ENABLE 0
#endif

#if CHAT_CONVERSATION_LOG_ENABLE
#define CHAT_CONVERSATION_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_CONVERSATION_LOG(...)
#endif

namespace chat
{
namespace ui
{

// Keep original constant meaning (bubble max width and 70% rule)
namespace
{
constexpr lv_coord_t kBubbleMaxWidth = 322; // same as original
constexpr lv_coord_t kBubblePadX = 10;      // keep in sync with styles.cpp
constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
constexpr uint32_t kSecondsPerMonth = 30U * kSecondsPerDay;
constexpr uint32_t kSecondsPerYear = 365U * kSecondsPerDay;
constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01
constexpr size_t kMaxPrefixedSenderLen = 20;
constexpr lv_coord_t kLocationMapBorderPx = 2;
constexpr lv_coord_t kLocationMapInnerSize = 188;
constexpr lv_coord_t kLocationMapOuterSize =
    kLocationMapInnerSize + (kLocationMapBorderPx * 2);
constexpr lv_coord_t kLocationMapFitPadding = 14;
constexpr lv_coord_t kMetaChipMinWidth = 18;

lv_coord_t bubble_pad_x()
{
    return ::ui::page_profile::is_dense() ? 6 : kBubblePadX;
}

void make_plain(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
}

lv_obj_t* create_meta_row(lv_obj_t* parent, lv_coord_t max_width, bool is_self)
{
    lv_obj_t* row = lv_obj_create(parent);
    make_plain(row);
    lv_obj_set_size(row,
                    std::max<lv_coord_t>(max_width, kMetaChipMinWidth),
                    LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row,
                          is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row,
                                ::ui::page_profile::is_dense() ? 3 : 4,
                                0);
    lv_obj_set_style_pad_row(row,
                             ::ui::page_profile::is_dense() ? 2 : 3,
                             0);
    return row;
}

lv_obj_t* create_meta_chip(lv_obj_t* parent,
                           const char* text,
                           lv_color_t bg_color,
                           lv_coord_t max_width)
{
    if (!parent || !text || text[0] == '\0')
    {
        return nullptr;
    }

    const bool dense = ::ui::page_profile::is_dense();
    const lv_coord_t pad_x = dense ? 6 : 8;
    const lv_coord_t pad_y = dense ? 2 : 3;
    const lv_coord_t safe_max_width =
        std::max<lv_coord_t>(max_width, kMetaChipMinWidth);

    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(chip, safe_max_width, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(chip, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(chip, dense ? 7 : 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(chip, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(chip, pad_x, LV_PART_MAIN);
    lv_obj_set_style_pad_right(chip, pad_x, LV_PART_MAIN);
    lv_obj_set_style_pad_top(chip, pad_y, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(chip, pad_y, LV_PART_MAIN);
    lv_obj_set_style_min_height(chip, dense ? 16 : 20, LV_PART_MAIN);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* label = lv_label_create(chip);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(
        label,
        std::max<lv_coord_t>(safe_max_width - (2 * pad_x), kMetaChipMinWidth),
        LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    ::ui::i18n::set_label_text_raw(label, text);
    ::ui::fonts::apply_localized_font(
        label,
        lv_label_get_text(label),
        ::ui::page_profile::resolve_caption_font());
    lv_obj_center(label);
    return label;
}

void set_hidden(lv_obj_t* obj, bool hidden)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return;
    }
    if (hidden)
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

bool overlay_item_has_valid_point(const ::ui::map::MapOverlayItem& item)
{
    return item.visible && item.point.valid &&
           std::isfinite(item.point.lat) &&
           std::isfinite(item.point.lon) &&
           item.point.lat >= -90.0 && item.point.lat <= 90.0 &&
           item.point.lon >= -180.0 && item.point.lon <= 180.0;
}

double normalize_longitude(double longitude)
{
    while (longitude < -180.0)
    {
        longitude += 360.0;
    }
    while (longitude >= 180.0)
    {
        longitude -= 360.0;
    }
    return longitude;
}

bool location_overlay_bounds(const ::ui::map::MapOverlaySnapshot& overlay,
                             double& out_min_lat,
                             double& out_max_lat,
                             double& out_center_lon,
                             std::size_t& out_count)
{
    out_min_lat = 90.0;
    out_max_lat = -90.0;
    out_center_lon = 0.0;
    out_count = 0;

    if (!overlay.header.valid)
    {
        return false;
    }

    for (std::size_t i = 0; i < overlay.item_count; ++i)
    {
        const auto& item = overlay.items[i];
        if (!overlay_item_has_valid_point(item))
        {
            continue;
        }
        out_min_lat = std::min(out_min_lat, item.point.lat);
        out_max_lat = std::max(out_max_lat, item.point.lat);
        ++out_count;
    }

    if (out_count == 0)
    {
        return false;
    }

    // Longitudes wrap at +/-180 degrees. Use the complement of the widest
    // gap so nearby points on opposite sides of the date line stay nearby.
    double widest_gap_deg = -1.0;
    double gap_start_lon = 0.0;
    for (std::size_t i = 0; i < overlay.item_count; ++i)
    {
        const auto& item = overlay.items[i];
        if (!overlay_item_has_valid_point(item))
        {
            continue;
        }

        const double start_lon = normalize_longitude(item.point.lon);
        double next_gap_deg = 360.0;
        for (std::size_t j = 0; j < overlay.item_count; ++j)
        {
            const auto& candidate = overlay.items[j];
            if (i == j || !overlay_item_has_valid_point(candidate))
            {
                continue;
            }

            double gap_deg = normalize_longitude(candidate.point.lon) - start_lon;
            if (gap_deg <= 0.0)
            {
                gap_deg += 360.0;
            }
            next_gap_deg = std::min(next_gap_deg, gap_deg);
        }

        if (next_gap_deg > widest_gap_deg)
        {
            widest_gap_deg = next_gap_deg;
            gap_start_lon = start_lon;
        }
    }

    const double covered_span_deg = 360.0 - widest_gap_deg;
    out_center_lon = normalize_longitude(
        gap_start_lon + widest_gap_deg + covered_span_deg / 2.0);
    return true;
}

bool model_fits_location_overlay(lv_obj_t* viewport_root,
                                 const ::ui::widgets::map::Model& model,
                                 const ::ui::map::MapOverlaySnapshot& overlay)
{
    if (!viewport_root || !lv_obj_is_valid(viewport_root))
    {
        return false;
    }

    lv_obj_update_layout(viewport_root);
    for (std::size_t i = 0; i < overlay.item_count; ++i)
    {
        const auto& item = overlay.items[i];
        if (!overlay_item_has_valid_point(item))
        {
            continue;
        }

        lv_point_t point{};
        const ::ui::widgets::map::GeoPoint geo{
            true,
            item.point.lat,
            item.point.lon};
        if (!::ui::widgets::map::preview_project_point(
                viewport_root,
                model,
                geo,
                point))
        {
            return false;
        }

        if (point.x < kLocationMapFitPadding ||
            point.y < kLocationMapFitPadding ||
            point.x > kLocationMapInnerSize - kLocationMapFitPadding ||
            point.y > kLocationMapInnerSize - kLocationMapFitPadding)
        {
            return false;
        }
    }
    return true;
}

::ui::chat::MessageDeliveryState delivery_from_message_status(
    chat::MessageStatus status)
{
    switch (status)
    {
    case chat::MessageStatus::Incoming:
        return ::ui::chat::MessageDeliveryState::Received;
    case chat::MessageStatus::Queued:
        return ::ui::chat::MessageDeliveryState::Queued;
    case chat::MessageStatus::Sent:
        return ::ui::chat::MessageDeliveryState::Sent;
    case chat::MessageStatus::Failed:
        return ::ui::chat::MessageDeliveryState::Failed;
    case chat::MessageStatus::Delivered:
        return ::ui::chat::MessageDeliveryState::Delivered;
    }
    return ::ui::chat::MessageDeliveryState::Unknown;
}

const char* delivery_status_text_key(::ui::chat::MessageDeliveryState delivery)
{
    switch (delivery)
    {
    case ::ui::chat::MessageDeliveryState::Queued:
        return "Queued";
    case ::ui::chat::MessageDeliveryState::Sending:
        return "Sending...";
    case ::ui::chat::MessageDeliveryState::Sent:
        return "Sent";
    case ::ui::chat::MessageDeliveryState::Delivered:
        return "Delivered";
    case ::ui::chat::MessageDeliveryState::Failed:
        return "Failed";
    case ::ui::chat::MessageDeliveryState::Draft:
    case ::ui::chat::MessageDeliveryState::Received:
    case ::ui::chat::MessageDeliveryState::Unknown:
        return nullptr;
    }
    return nullptr;
}

lv_color_t delivery_status_chip_color(::ui::chat::MessageDeliveryState delivery)
{
    switch (delivery)
    {
    case ::ui::chat::MessageDeliveryState::Queued:
        return lv_color_hex(0xF4E2B0);
    case ::ui::chat::MessageDeliveryState::Sending:
    case ::ui::chat::MessageDeliveryState::Sent:
        return lv_color_hex(0xCFE4FF);
    case ::ui::chat::MessageDeliveryState::Delivered:
        return lv_color_hex(0xD4F0D2);
    case ::ui::chat::MessageDeliveryState::Failed:
        return lv_color_hex(0xF1B8AA);
    case ::ui::chat::MessageDeliveryState::Draft:
    case ::ui::chat::MessageDeliveryState::Received:
    case ::ui::chat::MessageDeliveryState::Unknown:
        break;
    }
    return lv_color_hex(0xE8E0D8);
}

void update_delivery_status_chip(lv_obj_t* status_label,
                                 ::ui::chat::MessageDeliveryState delivery)
{
    if (!status_label)
    {
        return;
    }

    lv_obj_t* chip = lv_obj_get_parent(status_label);
    const char* text_key = delivery_status_text_key(delivery);
    if (!text_key || text_key[0] == '\0')
    {
        lv_label_set_text(status_label, "");
        if (chip)
        {
            lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (chip)
    {
        lv_obj_set_style_bg_color(chip,
                                  delivery_status_chip_color(delivery),
                                  LV_PART_MAIN);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_HIDDEN);
    }
    ::ui::i18n::set_label_text(status_label, text_key);
    ::ui::fonts::apply_localized_font(
        status_label,
        lv_label_get_text(status_label),
        ::ui::page_profile::resolve_caption_font());
}

bool message_ref_matches_id(const ::ui::chat::MessageRef& ref,
                            chat::MessageId msg_id,
                            bool has_protocol,
                            chat::MeshProtocol protocol)
{
    if (has_protocol && ref.protocol != 0 &&
        ref.protocol != static_cast<uint8_t>(protocol))
    {
        return false;
    }
    if (ref.protocol_id != 0)
    {
        return ref.protocol_id == msg_id;
    }
    return ref.local_id == static_cast<uint64_t>(msg_id);
}

uint32_t timestamp_from_presentation_label(const ::ui::FixedText<24>& label)
{
    const char* text = label.c_str();
    if (!text || text[0] == '\0')
    {
        return 0;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || (end && *end != '\0'))
    {
        return 0;
    }
    return static_cast<uint32_t>(value);
}

bool is_structured_team_payload(
    const ::ui::chat::TeamMessageRichPayload& payload)
{
    return payload.kind == ::ui::chat::TeamMessageRichPayloadKind::Location ||
           payload.kind == ::ui::chat::TeamMessageRichPayloadKind::Command;
}

std::string rich_payload_kind_label(
    ::ui::chat::TeamMessageRichPayloadKind kind)
{
    switch (kind)
    {
    case ::ui::chat::TeamMessageRichPayloadKind::Location:
        return "Location";
    case ::ui::chat::TeamMessageRichPayloadKind::Command:
        return "Command";
    case ::ui::chat::TeamMessageRichPayloadKind::Unsupported:
        return "Unsupported";
    case ::ui::chat::TeamMessageRichPayloadKind::Text:
        return "Text";
    case ::ui::chat::TeamMessageRichPayloadKind::None:
        break;
    }
    return "Team";
}

std::string format_team_rich_payload_text(
    const ::ui::chat::TeamMessageRichPayload& payload)
{
    if (!payload.summary.empty())
    {
        return payload.summary.c_str();
    }
    if (!payload.title.empty())
    {
        return payload.title.c_str();
    }
    return rich_payload_kind_label(payload.kind);
}

const char* message_ingress_label(::ui::chat::MessageIngressTransport transport)
{
    switch (transport)
    {
    case ::ui::chat::MessageIngressTransport::LoRa:
        return "LoRa";
    case ::ui::chat::MessageIngressTransport::Mqtt:
        return "MQTT";
    case ::ui::chat::MessageIngressTransport::WiFi:
        return "Wi-Fi";
    case ::ui::chat::MessageIngressTransport::Unknown:
        break;
    }
    return nullptr;
}
} // namespace

static bool is_valid_epoch_ts(uint32_t ts)
{
    return ts >= kMinValidEpochSeconds;
}

static bool format_absolute_message_time(char* out, size_t out_len, uint32_t ts)
{
    if (!out || out_len == 0 || !is_valid_epoch_ts(ts))
    {
        return false;
    }
    const std::time_t raw = static_cast<std::time_t>(ts);
    std::tm* tm = std::localtime(&raw);
    if (!tm)
    {
        return false;
    }
    std::snprintf(out,
                  out_len,
                  "%02u/%02u %02u:%02u",
                  static_cast<unsigned>(tm->tm_mon + 1),
                  static_cast<unsigned>(tm->tm_mday),
                  static_cast<unsigned>(tm->tm_hour),
                  static_cast<unsigned>(tm->tm_min));
    return true;
}

static void format_message_time(char* out, size_t out_len, uint32_t ts)
{
    if (!out || out_len == 0) return;
    if (!is_valid_epoch_ts(ts))
    {
        snprintf(out, out_len, "--");
        return;
    }

    uint32_t now_epoch = sys::epoch_seconds_now();
    if (!is_valid_epoch_ts(now_epoch))
    {
        if (!format_absolute_message_time(out, out_len, ts))
        {
            snprintf(out, out_len, "--");
        }
        return;
    }

    uint32_t now_secs = now_epoch;
    if (now_secs < ts)
    {
        now_secs = ts;
    }
    uint32_t diff = now_secs - ts;

    if (diff < 60U)
    {
        snprintf(out, out_len, "%s", ::ui::i18n::tr("now"));
        return;
    }
    if (diff < 3600U)
    {
        snprintf(out,
                 out_len,
                 "%u min ago",
                 static_cast<unsigned>(diff / 60U));
        return;
    }
    if (diff < kSecondsPerDay)
    {
        snprintf(out,
                 out_len,
                 "%u hr ago",
                 static_cast<unsigned>(diff / 3600U));
        return;
    }
    if (diff < kSecondsPerMonth)
    {
        const unsigned days = static_cast<unsigned>(diff / kSecondsPerDay);
        snprintf(out,
                 out_len,
                 "%u day%s ago",
                 days,
                 days == 1U ? "" : "s");
        return;
    }
    if (diff < kSecondsPerYear)
    {
        snprintf(out,
                 out_len,
                 "%u mo ago",
                 static_cast<unsigned>(diff / kSecondsPerMonth));
        return;
    }
    snprintf(out,
             out_len,
             "%u yr ago",
             static_cast<unsigned>(diff / kSecondsPerYear));
}

static bool sender_token_is_valid(const std::string& sender)
{
    if (sender.empty() || sender.size() > kMaxPrefixedSenderLen)
    {
        return false;
    }
    for (char c : sender)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.'))
        {
            return false;
        }
    }
    return true;
}

static bool split_prefixed_sender_text(const std::string& text,
                                       std::string* out_sender,
                                       std::string* out_body)
{
    if (!out_sender || !out_body || text.empty())
    {
        return false;
    }

    const size_t sep = text.find(':');
    if (sep == std::string::npos || sep == 0 || sep > kMaxPrefixedSenderLen)
    {
        return false;
    }

    std::string sender = text.substr(0, sep);
    while (!sender.empty() && sender.back() == ' ')
    {
        sender.pop_back();
    }
    if (!sender_token_is_valid(sender))
    {
        return false;
    }

    size_t body_start = sep + 1;
    while (body_start < text.size() && text[body_start] == ' ')
    {
        ++body_start;
    }
    if (body_start >= text.size())
    {
        return false;
    }

    *out_sender = sender;
    *out_body = text.substr(body_start);
    return true;
}

ChatConversationScreen::ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv)
    : conv_(conv)
{
    guard_ = new LifetimeGuard();
    guard_->alive = true;
    guard_->pending_async = 0;

    lv_obj_t* active = lv_screen_active();
    if (!active)
    {
        CHAT_CONVERSATION_LOG("[ChatConversation] WARNING: lv_screen_active() is null\n");
    }
    else
    {
        CHAT_CONVERSATION_LOG("[ChatConversation] init: active=%p parent=%p\n", active, parent);
    }

    // ----- Layout -----
    auto w = chat::ui::layout::create_conversation_base(parent);
    container_ = w.root;
    body_row_ = w.body_row;
    right_column_ = w.right_column;
    msg_list_ = w.msg_list;
    action_bar_ = w.action_bar;
    reply_btn_ = w.reply_btn;

    // ----- Styles -----
    chat::ui::conversation::styles::apply_root(container_);
    chat::ui::conversation::styles::apply_msg_list(msg_list_);
    chat::ui::conversation::styles::apply_action_bar(action_bar_);
    chat::ui::conversation::styles::apply_reply_btn(reply_btn_);
    if (msg_list_)
    {
        lv_obj_add_event_cb(msg_list_, scroll_event_cb, LV_EVENT_SCROLL, this);
    }

    // Primary compose entry label.
    ::ui::i18n::set_label_text(w.reply_label, "Send");
    chat::ui::conversation::styles::apply_reply_label(w.reply_label);
    ::ui::fonts::apply_localized_font(w.reply_label, lv_label_get_text(w.reply_label), ::ui::fonts::ui_chrome_font());

    // ----- Top bar (existing widget, unchanged behavior) -----
    ::ui::widgets::top_bar_init(top_bar_, container_);
    const char* title = (conv_.peer == 0) ? ::ui::i18n::tr("Broadcast") : ::ui::i18n::tr("Direct");
    ::ui::widgets::top_bar_set_title(top_bar_, title);
    ::ui::widgets::top_bar_set_back_callback(top_bar_, handle_back, this);
    if (top_bar_.container)
    {
        lv_obj_move_to_index(top_bar_.container, 0);
    }

    if (container_)
    {
        lv_obj_add_event_cb(container_, on_root_deleted, LV_EVENT_DELETE, this);
    }

    if (container_ && !lv_obj_is_valid(container_))
    {
        CHAT_CONVERSATION_LOG("[ChatConversation] WARNING: container invalid\n");
    }
    if (msg_list_ && !lv_obj_is_valid(msg_list_))
    {
        CHAT_CONVERSATION_LOG("[ChatConversation] WARNING: msg_list invalid\n");
    }

    // ----- Event (unchanged) -----
    reply_ctx_.screen = this;
    reply_ctx_.intent = ActionIntent::Reply;
    lv_obj_add_event_cb(reply_btn_, action_event_cb, LV_EVENT_CLICKED, &reply_ctx_);

    // ----- Input layer (explicit, v0 no-op) -----
    chat::ui::conversation::input::init(this, &input_binding_);
}

ChatConversationScreen::~ChatConversationScreen()
{
    ::ui::components::shortcut_help_modal::close(shortcut_help_modal_);
    ::ui::widgets::map::destroy(location_map_runtime_);
    location_map_created_ = false;
    if (container_ && lv_obj_is_valid(container_))
    {
        lv_obj_del(container_);
    }
    if (guard_)
    {
        guard_->alive = false;
        if (guard_->pending_async == 0)
        {
            delete guard_;
        }
        guard_ = nullptr;
    }
}

void ChatConversationScreen::addMessage(const ::ui::chat::MessageRow& row)
{
    const uint32_t started_ms = lv_tick_get();
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=conversation_add begin local_id=%llu protocol_id=%lu existing=%u outgoing=%u delivery=%u\n",
                          static_cast<unsigned long long>(row.ref.local_id),
                          static_cast<unsigned long>(row.ref.protocol_id),
                          static_cast<unsigned>(messages_.size()),
                          row.outgoing ? 1U : 0U,
                          static_cast<unsigned>(row.delivery));
    if (!guard_ || !guard_->alive || !msg_list_ || !lv_obj_is_valid(msg_list_))
    {
        CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=conversation_add reject guard=%u alive=%u list=%p valid=%u\n",
                              guard_ ? 1U : 0U,
                              guard_ && guard_->alive ? 1U : 0U,
                              msg_list_,
                              msg_list_ && lv_obj_is_valid(msg_list_) ? 1U : 0U);
        return;
    }
    if (messages_.size() >= MAX_DISPLAY_MESSAGES)
    {
        MessageItem& oldest = messages_[0];
        if (oldest.container)
        {
            lv_obj_del(oldest.container);
        }
        messages_.erase(messages_.begin());
    }

    createMessageItem(row);
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=conversation_add item_done count=%u elapsed_ms=%lu\n",
                          static_cast<unsigned>(messages_.size()),
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=conversation_add end count=%u elapsed_ms=%lu\n",
                          static_cast<unsigned>(messages_.size()),
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));
}

void ChatConversationScreen::clearMessages()
{
    const uint32_t started_ms = lv_tick_get();
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=conversation_clear begin count=%u guard=%u alive=%u\n",
                          static_cast<unsigned>(messages_.size()),
                          guard_ ? 1U : 0U,
                          guard_ && guard_->alive ? 1U : 0U);
    if (!guard_ || !guard_->alive)
    {
        CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=conversation_clear reject\n");
        return;
    }
    size_t index = 0;
    for (auto& item : messages_)
    {
        if (item.container)
        {
            CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=conversation_clear delete index=%u elapsed_ms=%lu\n",
                                  static_cast<unsigned>(index),
                                  static_cast<unsigned long>(lv_tick_elaps(started_ms)));
            lv_obj_del(item.container);
        }
        ++index;
    }
    messages_.clear();
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=conversation_clear end elapsed_ms=%lu\n",
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));
}

void ChatConversationScreen::scrollToTop()
{
    if (guard_ && guard_->alive && msg_list_)
    {
        lv_obj_scroll_to_y(msg_list_, 0, LV_ANIM_OFF);
        history_last_scroll_y_ = lv_obj_get_scroll_y(msg_list_);
        history_scroll_position_valid_ = true;
    }
}

void ChatConversationScreen::scrollToBottom()
{
    if (guard_ && guard_->alive && msg_list_)
    {
        lv_obj_scroll_to_y(msg_list_, LV_COORD_MAX, LV_ANIM_OFF);
        history_last_scroll_y_ = lv_obj_get_scroll_y(msg_list_);
        history_scroll_position_valid_ = true;
    }
}

bool ChatConversationScreen::updateMessageStatus(const chat::MessageId msg_id,
                                                 const chat::MessageStatus status,
                                                 bool has_protocol,
                                                 chat::MeshProtocol protocol)
{
    if (!guard_ || !guard_->alive || msg_id == 0)
    {
        return false;
    }

    for (auto& item : messages_)
    {
        if (!message_ref_matches_id(item.ref, msg_id, has_protocol, protocol))
        {
            continue;
        }
        item.delivery = delivery_from_message_status(status);
        if (!item.status_label)
        {
            return true;
        }

        update_delivery_status_chip(item.status_label, item.delivery);
        if (status == MessageStatus::Failed)
        {
            enableRetryAction(item);
        }
        else
        {
            disableRetryAction(item);
        }
        return true;
    }

    return false;
}

void ChatConversationScreen::setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    action_cb_ = cb;
    action_cb_user_data_ = user_data;
}

void ChatConversationScreen::setMessageActionCallback(
    void (*cb)(MessageActionIntent intent,
               ::ui::chat::MessageRef ref,
               void*),
    void* user_data)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    message_action_cb_ = cb;
    message_action_cb_user_data_ = user_data;
}

void ChatConversationScreen::setHeaderText(const char* title, const char* status)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    ::ui::widgets::top_bar_set_title(top_bar_, title);
    if (status != nullptr)
    {
        ::ui::widgets::top_bar_set_right_text(top_bar_, status);
    }
}

void ChatConversationScreen::updateBatteryFromBoard()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
}

void ChatConversationScreen::setBackCallback(void (*cb)(void*), void* user_data)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    back_cb_ = cb;
    back_cb_user_data_ = user_data;
}

void ChatConversationScreen::setReplyEnabled(bool enabled)
{
    if (!guard_ || !guard_->alive || !reply_btn_)
    {
        return;
    }
    reply_enabled_ = enabled;
    if (enabled)
    {
        lv_obj_clear_state(reply_btn_, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(reply_btn_, LV_STATE_DISABLED);
    }
}

bool ChatConversationScreen::requestAction(ActionIntent intent)
{
    if (!guard_ || !guard_->alive)
    {
        return false;
    }

    if (intent == ActionIntent::Reply && !reply_enabled_)
    {
        ::ui::feedback::show_notice("Cannot send here", 1200);
        return true;
    }

    if (!action_cb_)
    {
        return false;
    }

    if (intent == ActionIntent::LoadOlder)
    {
        if (history_auto_load_pending_)
        {
            return true;
        }
        if (!history_has_older_)
        {
            if (!history_older_boundary_notified_)
            {
                history_older_boundary_notified_ = true;
                ::ui::feedback::show_notice("No more messages", 1400);
            }
            return true;
        }
        history_auto_load_pending_ = true;
    }
    else if (intent == ActionIntent::LoadNewer)
    {
        if (history_auto_load_pending_)
        {
            return true;
        }
        if (!history_has_newer_)
        {
            if (!history_newer_boundary_notified_)
            {
                history_newer_boundary_notified_ = true;
                ::ui::feedback::show_notice("Latest messages", 1400);
            }
            return true;
        }
        history_auto_load_pending_ = true;
    }

    schedule_action_async(intent);
    return true;
}

void ChatConversationScreen::toggleShortcutHelp()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    if (::ui::components::shortcut_help_modal::is_open(shortcut_help_modal_))
    {
        ::ui::components::shortcut_help_modal::close(shortcut_help_modal_);
        return;
    }

    static constexpr ::ui::components::shortcut_help_modal::Row rows[] = {
        {"S", nullptr, "Compose message"},
        {"Up/Down", nullptr, "Scroll messages"},
        {"Prev/Next", nullptr, "Load older/newer"},
        {"Home/End", nullptr, "Top/bottom"},
        {"M", nullptr, "Show or hide map"},
        {"L", nullptr, "Cycle map layer"},
        {"Back", "Esc", "Conversation list"},
        {"H", nullptr, "Close help"},
    };

    lv_obj_t* parent =
        container_ && lv_obj_is_valid(container_) ? container_ : lv_screen_active();
    if (!parent)
    {
        return;
    }

    ::ui::components::shortcut_help_modal::Config config{};
    config.title = "Conversation Help";
    config.rows = rows;
    config.row_count = sizeof(rows) / sizeof(rows[0]);
    config.width = ::ui::page_profile::is_dense() ? 288 : 304;
    config.height = ::ui::page_profile::is_dense() ? 170 : 186;
    config.restore_group = lv_group_get_default();
    (void)::ui::components::shortcut_help_modal::open(
        shortcut_help_modal_,
        parent,
        config);
}

void ChatConversationScreen::setHistoryPaging(bool has_older,
                                              bool has_newer,
                                              uint16_t offset,
                                              uint16_t total_count)
{
    history_has_older_ = has_older;
    history_has_newer_ = has_newer;
    history_offset_ = offset;
    history_total_count_ = total_count;
    history_auto_load_pending_ = false;
    history_scroll_position_valid_ = false;
    history_last_scroll_y_ = 0;
    history_older_boundary_notified_ = false;
    history_newer_boundary_notified_ = false;
}

void ChatConversationScreen::handleScroll()
{
    if (!guard_ || !guard_->alive || !msg_list_ || !lv_obj_is_valid(msg_list_))
    {
        return;
    }

    const lv_coord_t scroll_y = lv_obj_get_scroll_y(msg_list_);
    if (!history_scroll_position_valid_)
    {
        history_scroll_position_valid_ = true;
        history_last_scroll_y_ = scroll_y;
        return;
    }

    const bool scrolling_toward_older = scroll_y < history_last_scroll_y_;
    const bool scrolling_toward_newer = scroll_y > history_last_scroll_y_;
    history_last_scroll_y_ = scroll_y;

    const bool at_top = scroll_y <= 0;
    const bool at_bottom = lv_obj_get_scroll_bottom(msg_list_) <= 0;
    if (!at_top)
    {
        history_older_boundary_notified_ = false;
    }
    if (!at_bottom)
    {
        history_newer_boundary_notified_ = false;
    }

    if (history_auto_load_pending_ || !action_cb_)
    {
        return;
    }

    if (scrolling_toward_older && at_top)
    {
        (void)requestAction(ActionIntent::LoadOlder);
        return;
    }

    if (scrolling_toward_newer && at_bottom)
    {
        (void)requestAction(ActionIntent::LoadNewer);
    }
}

void ChatConversationScreen::setLocationOverlay(
    const ::ui::map::MapOverlaySnapshot& overlay)
{
    location_overlay_ = overlay;
    if (location_map_visible_)
    {
        refreshLocationMap();
    }
}

void ChatConversationScreen::toggleLocationMap()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }

    location_map_visible_ = !location_map_visible_;
    syncLocationMapVisibility();
}

void ChatConversationScreen::cycleLocationMapLayer()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }

    const auto layers = ::ui::widgets::map::current_layer_state();
    const uint8_t next_source =
        static_cast<uint8_t>((layers.map_source + 1U) % 3U);
    (void)::ui::widgets::map::set_layer_map_source(next_source);

    if (location_map_visible_)
    {
        refreshLocationMap();
    }
}

bool ChatConversationScreen::usesFloatingLocationMap() const
{
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
    return true;
#else
    const auto& profile = ::ui::page_profile::current();
    return profile.name != nullptr && std::strcmp(profile.name, "tdeck") == 0;
#endif
}

void ChatConversationScreen::createLocationPanel()
{
    if (location_panel_ && lv_obj_is_valid(location_panel_))
    {
        return;
    }
    if (!container_ || !lv_obj_is_valid(container_))
    {
        return;
    }

    const bool floating = usesFloatingLocationMap();
    lv_obj_t* parent = floating ? container_ : body_row_;
    if (!parent || !lv_obj_is_valid(parent))
    {
        return;
    }

    location_panel_ = lv_obj_create(parent);
    lv_obj_set_size(location_panel_,
                    kLocationMapOuterSize,
                    kLocationMapOuterSize);
    lv_obj_clear_flag(location_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(location_panel_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(location_panel_, 0, 0);
    lv_obj_set_style_radius(location_panel_, 0, 0);
    lv_obj_set_style_bg_color(location_panel_, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(location_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(location_panel_, kLocationMapBorderPx, 0);
    lv_obj_set_style_border_color(location_panel_, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_opa(location_panel_, LV_OPA_COVER, 0);
    lv_obj_set_flex_grow(location_panel_, 0);
    lv_obj_add_flag(location_panel_, LV_OBJ_FLAG_HIDDEN);

    if (floating)
    {
#ifdef LV_OBJ_FLAG_IGNORE_LAYOUT
        lv_obj_add_flag(location_panel_, LV_OBJ_FLAG_IGNORE_LAYOUT);
#endif
        lv_obj_align(location_panel_,
                     LV_ALIGN_TOP_LEFT,
                     kLocationMapBorderPx,
                     static_cast<lv_coord_t>(
                         ::ui::page_profile::current().top_bar_height +
                         kLocationMapBorderPx));
    }
    else
    {
        lv_obj_move_to_index(location_panel_, 0);
    }

    location_map_host_ = lv_obj_create(location_panel_);
    make_plain(location_map_host_);
    lv_obj_set_size(location_map_host_,
                    kLocationMapInnerSize,
                    kLocationMapInnerSize);
    lv_obj_center(location_map_host_);
}

void ChatConversationScreen::ensureLocationMapCreated()
{
    createLocationPanel();
    if (location_map_created_ ||
        !location_map_host_ ||
        !lv_obj_is_valid(location_map_host_))
    {
        return;
    }

    (void)::ui::widgets::map::create(
        location_map_runtime_,
        location_map_host_,
        75);
    ::ui::widgets::map::set_size(
        location_map_runtime_,
        kLocationMapInnerSize,
        kLocationMapInnerSize);
    ::ui::widgets::map::set_gesture_enabled(location_map_runtime_, false);
    location_map_created_ = true;
}

void ChatConversationScreen::refreshLocationMap()
{
    if (!location_map_visible_)
    {
        return;
    }

    ensureLocationMapCreated();
    if (!location_map_created_ ||
        !location_map_host_ ||
        !lv_obj_is_valid(location_map_host_))
    {
        return;
    }

    double min_lat = 0.0;
    double max_lat = 0.0;
    double center_lon = 0.0;
    std::size_t point_count = 0;
    if (!location_overlay_bounds(location_overlay_,
                                 min_lat,
                                 max_lat,
                                 center_lon,
                                 point_count))
    {
        ::ui::widgets::map::clear(location_map_runtime_);
        return;
    }

    ::ui::widgets::map::Model model{};
    const auto layers = ::ui::widgets::map::current_layer_state();
    model.focus_point.valid = true;
    model.focus_point.lat = (min_lat + max_lat) / 2.0;
    model.focus_point.lon = center_lon;
    model.map_source = layers.map_source;
    model.contour_enabled = layers.contour_enabled;
    model.coord_system = app::configFacade().readConfig().map_coord_system;

    if (point_count <= 1)
    {
        model.zoom = ::ui::widgets::map::kDefaultZoom;
    }
    else
    {
        model.zoom = ::ui::widgets::map::kMinZoom;
        for (int zoom = ::ui::widgets::map::kMaxZoom;
             zoom >= ::ui::widgets::map::kMinZoom;
             --zoom)
        {
            model.zoom = zoom;
            if (model_fits_location_overlay(
                    location_map_host_,
                    model,
                    location_overlay_))
            {
                break;
            }
        }
    }

    ::ui::widgets::map::apply_model(location_map_runtime_, model);
    ::ui::widgets::map::apply_overlay(location_map_runtime_, location_overlay_);
}

void ChatConversationScreen::syncLocationMapVisibility()
{
    if (!location_map_visible_)
    {
        set_hidden(location_panel_, true);
        if (location_map_created_)
        {
            ::ui::widgets::map::clear(location_map_runtime_);
        }
        return;
    }

    ensureLocationMapCreated();
    set_hidden(location_panel_, false);
    if (location_panel_ && lv_obj_is_valid(location_panel_))
    {
        if (usesFloatingLocationMap())
        {
            lv_obj_align(location_panel_,
                         LV_ALIGN_TOP_LEFT,
                         kLocationMapBorderPx,
                         static_cast<lv_coord_t>(
                             ::ui::page_profile::current().top_bar_height +
                             kLocationMapBorderPx));
            lv_obj_move_foreground(location_panel_);
        }
        else
        {
            lv_obj_move_to_index(location_panel_, 0);
        }
    }
    refreshLocationMap();
}

void ChatConversationScreen::createMessageItem(const ::ui::chat::MessageRow& row)
{
    const uint32_t started_ms = lv_tick_get();
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=create_message begin local_id=%llu protocol_id=%lu outgoing=%u delivery=%u text_len=%u\n",
                          static_cast<unsigned long long>(row.ref.local_id),
                          static_cast<unsigned long>(row.ref.protocol_id),
                          row.outgoing ? 1U : 0U,
                          static_cast<unsigned>(row.delivery),
                          static_cast<unsigned>(std::strlen(row.text.c_str())));
    if (!guard_ || !guard_->alive || !msg_list_)
    {
        CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=create_message reject guard=%u alive=%u list=%p\n",
                              guard_ ? 1U : 0U,
                              guard_ && guard_->alive ? 1U : 0U,
                              msg_list_);
        return;
    }
    MessageItem item;
    item.ref = row.ref;
    item.delivery = row.delivery;

    // ----- Layout: row + bubble + time + text + status -----
    item.container = chat::ui::layout::create_message_row(msg_list_);
    chat::ui::conversation::styles::apply_message_row(item.container);

    const bool is_self = row.outgoing;
    std::string display_text =
        row.has_team_rich_payload
            ? format_team_rich_payload_text(row.team_rich_payload)
            : std::string(row.text.c_str());
    std::string inferred_sender;
    if (!is_self &&
        conv_.protocol == chat::MeshProtocol::MeshCore &&
        conv_.peer == 0 &&
        row.sender_node_id == 0)
    {
        std::string parsed_sender;
        std::string parsed_body;
        if (split_prefixed_sender_text(display_text, &parsed_sender, &parsed_body))
        {
            inferred_sender = parsed_sender;
            display_text = parsed_body;
        }
    }

    // Compute bubble max width (same logic as original)
    lv_coord_t max_bubble_w = kBubbleMaxWidth;
    lv_coord_t list_w = chat::ui::layout::get_msg_list_content_width(msg_list_);
    if (list_w > 0)
    {
        // original: candidate = (list_w - 2 * kPadX) * 7 / 10
        // kPadX lives in styles; but for behavior parity we replicate formula using known 8px.
        const lv_coord_t kPadX = ::ui::page_profile::is_dense() ? 4 : 8;
        lv_coord_t candidate = (list_w - 2 * kPadX) * 7 / 10;
        if (candidate > 0 && candidate < max_bubble_w)
        {
            max_bubble_w = candidate;
        }
    }

    lv_obj_t* bubble = chat::ui::layout::create_bubble(item.container);
    item.bubble = bubble;
    chat::ui::conversation::styles::apply_bubble(
        bubble, is_self, row.source_unverified);
    chat::ui::layout::set_bubble_max_width(bubble, max_bubble_w);

    char time_buf[24];
    format_message_time(
        time_buf,
        sizeof(time_buf),
        timestamp_from_presentation_label(row.time_label));
    const char* ingress_label = !is_self ? message_ingress_label(row.ingress_transport) : nullptr;

    std::string sender;
    if (is_self)
    {
        sender = app::configFacade().readConfig().short_name;
        if (sender.empty() && !row.sender_label.empty())
        {
            sender = row.sender_label.c_str();
        }
        if (sender.empty())
        {
            sender = "Me";
        }
    }
    else if (!row.sender_label.empty())
    {
        sender = inferred_sender.empty() ? row.sender_label.c_str() : inferred_sender;
    }
    else if (row.sender_node_id == 0)
    {
        sender = inferred_sender.empty() ? ::ui::i18n::tr("Unknown") : inferred_sender;
    }
    else
    {
        sender = app::messagingFacade().getContactService().getContactName(
            row.sender_node_id);
        if (sender.empty())
        {
            char buf[16];
            snprintf(buf,
                     sizeof(buf),
                     "%04lX",
                     static_cast<unsigned long>(row.sender_node_id & 0xFFFF));
            sender = buf;
        }
    }

    const lv_coord_t max_meta_w =
        std::max<lv_coord_t>(max_bubble_w - 2 * bubble_pad_x(), 24);
    item.meta_row = create_meta_row(bubble, max_meta_w, is_self);
    item.sender_label =
        create_meta_chip(item.meta_row, sender.c_str(), lv_color_hex(0xF1B75A), max_meta_w);
    if (ingress_label && ingress_label[0] != '\0')
    {
        item.source_label = create_meta_chip(
            item.meta_row,
            ingress_label,
            lv_color_hex(0xCFE4FF),
            max_meta_w);
    }
    item.time_label =
        create_meta_chip(item.meta_row, time_buf, lv_color_hex(0xD4F0D2), max_meta_w);
    if (is_self)
    {
        item.status_label = create_meta_chip(item.meta_row,
                                             "Sending...",
                                             delivery_status_chip_color(row.delivery),
                                             max_meta_w);
        update_delivery_status_chip(item.status_label, row.delivery);
    }
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=create_message metadata_done local_id=%llu elapsed_ms=%lu\n",
                          static_cast<unsigned long long>(row.ref.local_id),
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));

    item.text_label = chat::ui::layout::create_bubble_text(bubble);
    chat::ui::conversation::styles::apply_bubble_text(item.text_label);
    if (row.has_team_rich_payload &&
        is_structured_team_payload(row.team_rich_payload))
    {
        lv_obj_t* badge_label = chat::ui::layout::create_bubble_text(bubble);
        chat::ui::conversation::styles::apply_bubble_time(badge_label);
        const std::string badge =
            row.team_rich_payload.badge.empty()
                ? rich_payload_kind_label(row.team_rich_payload.kind)
                : std::string(row.team_rich_payload.badge.c_str());
        lv_label_set_text(badge_label, badge.c_str());
        ::ui::fonts::apply_localized_font(
            badge_label,
            lv_label_get_text(badge_label),
            ::ui::fonts::ui_chrome_font());
        const lv_coord_t max_badge_w =
            std::max<lv_coord_t>(max_bubble_w - 2 * bubble_pad_x(), 24);
        lv_obj_set_width(badge_label, max_badge_w);

        if (!row.team_rich_payload.title.empty())
        {
            lv_obj_t* title_label = chat::ui::layout::create_bubble_text(bubble);
            chat::ui::conversation::styles::apply_bubble_text(title_label);
            lv_label_set_text(title_label,
                              row.team_rich_payload.title.c_str());
            ::ui::fonts::apply_chat_content_font(
                title_label,
                row.team_rich_payload.title.c_str());
            lv_obj_set_width(title_label, max_badge_w);
        }
    }
    lv_label_set_text(item.text_label, display_text.c_str());
    const uint32_t font_started_ms = lv_tick_get();
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=create_message font_begin local_id=%llu text_len=%u elapsed_ms=%lu\n",
                          static_cast<unsigned long long>(row.ref.local_id),
                          static_cast<unsigned>(display_text.size()),
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));
    ::ui::fonts::apply_chat_content_font(item.text_label, display_text.c_str());
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=create_message font_done local_id=%llu font_ms=%lu elapsed_ms=%lu\n",
                          static_cast<unsigned long long>(row.ref.local_id),
                          static_cast<unsigned long>(lv_tick_elaps(font_started_ms)),
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));

    const lv_coord_t max_text_w =
        std::max<lv_coord_t>(max_bubble_w - 2 * bubble_pad_x(), 24);
    const uint32_t measure_started_ms = lv_tick_get();
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=create_message measure_begin local_id=%llu elapsed_ms=%lu\n",
                          static_cast<unsigned long long>(row.ref.local_id),
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));
    lv_point_t natural_text_size{};
    lv_text_get_size(
        &natural_text_size,
        display_text.c_str(),
        lv_obj_get_style_text_font(item.text_label, LV_PART_MAIN),
        lv_obj_get_style_text_letter_space(item.text_label, LV_PART_MAIN),
        lv_obj_get_style_text_line_space(item.text_label, LV_PART_MAIN),
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE);
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=create_message measure_done local_id=%llu measure_ms=%lu width=%ld elapsed_ms=%lu\n",
                          static_cast<unsigned long long>(row.ref.local_id),
                          static_cast<unsigned long>(lv_tick_elaps(measure_started_ms)),
                          static_cast<long>(natural_text_size.x),
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));
    const lv_coord_t natural_text_w = natural_text_size.x;
    if (natural_text_w > max_text_w)
    {
        lv_obj_set_width(item.text_label, max_text_w);
    }
    else
    {
        lv_obj_set_width(item.text_label, LV_SIZE_CONTENT);
    }

    if (row.delivery == ::ui::chat::MessageDeliveryState::Failed)
    {
        enableRetryAction(item);
    }

    // Align row based on sender (same behavior)
    chat::ui::layout::align_message_row(item.container, is_self);

    messages_.push_back(std::move(item));
    CHAT_CONVERSATION_LOG("[ChatUiTrace] stage=create_message end local_id=%llu count=%u elapsed_ms=%lu\n",
                          static_cast<unsigned long long>(row.ref.local_id),
                          static_cast<unsigned>(messages_.size()),
                          static_cast<unsigned long>(lv_tick_elaps(started_ms)));
}

void ChatConversationScreen::enableRetryAction(MessageItem& item)
{
    if (!item.bubble || item.retry_enabled)
    {
        return;
    }
    if (!item.retry_ctx)
    {
        item.retry_ctx.reset(new MessageActionContext());
    }
    item.retry_ctx->screen = this;
    item.retry_ctx->intent = MessageActionIntent::Retry;
    item.retry_ctx->ref = item.ref;
    lv_obj_add_flag(item.bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        item.bubble,
        message_action_event_cb,
        LV_EVENT_CLICKED,
        item.retry_ctx.get());
    item.retry_enabled = true;
}

void ChatConversationScreen::disableRetryAction(MessageItem& item)
{
    if (!item.bubble || !item.retry_enabled)
    {
        return;
    }
    lv_obj_remove_event_cb(item.bubble, message_action_event_cb);
    lv_obj_clear_flag(item.bubble, LV_OBJ_FLAG_CLICKABLE);
    item.retry_enabled = false;
    if (item.retry_ctx)
    {
        item.retry_ctx->screen = nullptr;
    }
}

void ChatConversationScreen::action_event_cb(lv_event_t* e)
{
    auto* ctx = static_cast<ActionContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->screen)
    {
        return;
    }
    ChatConversationScreen* screen = ctx->screen;
    if (!screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    (void)screen->requestAction(ctx->intent);
}

void ChatConversationScreen::message_action_event_cb(lv_event_t* e)
{
    auto* ctx = static_cast<MessageActionContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->screen)
    {
        return;
    }
    ChatConversationScreen* screen = ctx->screen;
    if (!screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->schedule_message_action_async(ctx->intent, ctx->ref);
}

void ChatConversationScreen::scroll_event_cb(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    screen->handleScroll();
}

void ChatConversationScreen::async_action_cb(void* user_data)
{
    auto* payload = static_cast<ActionPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->action_cb)
    {
        payload->action_cb(payload->intent, payload->user_data);
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

void ChatConversationScreen::async_message_action_cb(void* user_data)
{
    auto* payload = static_cast<MessageActionPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->message_action_cb)
    {
        payload->message_action_cb(
            payload->intent,
            payload->ref,
            payload->user_data);
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

void ChatConversationScreen::on_root_deleted(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    screen->handle_root_deleted();
}

void ChatConversationScreen::handle_back(void* user_data)
{
    ChatConversationScreen* screen = static_cast<ChatConversationScreen*>(user_data);
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->schedule_back_async();
}

void ChatConversationScreen::async_back_cb(void* user_data)
{
    auto* payload = static_cast<BackPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->back_cb)
    {
        payload->back_cb(payload->user_data);
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

lv_timer_t* ChatConversationScreen::add_timer(lv_timer_cb_t cb,
                                              uint32_t period_ms,
                                              void* user_data,
                                              TimerDomain domain)
{
    if (!guard_ || !guard_->alive)
    {
        return nullptr;
    }
    lv_timer_t* timer = lv_timer_create(cb, period_ms, user_data);
    if (timer)
    {
        TimerEntry entry;
        entry.timer = timer;
        entry.domain = domain;
        timers_.push_back(entry);
    }
    return timer;
}

void ChatConversationScreen::clear_timers(TimerDomain domain)
{
    if (timers_.empty())
    {
        return;
    }
    for (auto& entry : timers_)
    {
        if (entry.timer && entry.domain == domain)
        {
            lv_timer_del(entry.timer);
            entry.timer = nullptr;
        }
    }
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const TimerEntry& entry)
                       { return entry.timer == nullptr; }),
        timers_.end());
}

void ChatConversationScreen::clear_all_timers()
{
    for (auto& entry : timers_)
    {
        if (entry.timer)
        {
            lv_timer_del(entry.timer);
            entry.timer = nullptr;
        }
    }
    timers_.clear();
}

void ChatConversationScreen::handle_root_deleted()
{
    if ((!guard_ || !guard_->alive) && container_ == nullptr && msg_list_ == nullptr)
    {
        return;
    }

    if (guard_)
    {
        guard_->alive = false;
    }
    action_cb_ = nullptr;
    action_cb_user_data_ = nullptr;
    message_action_cb_ = nullptr;
    message_action_cb_user_data_ = nullptr;
    back_cb_ = nullptr;
    back_cb_user_data_ = nullptr;
    reply_ctx_.screen = nullptr;
    history_auto_load_pending_ = false;
    history_scroll_position_valid_ = false;
    history_last_scroll_y_ = 0;
    history_older_boundary_notified_ = false;
    history_newer_boundary_notified_ = false;

    ::ui::components::shortcut_help_modal::close(shortcut_help_modal_);
    chat::ui::conversation::input::cleanup(&input_binding_);
    ::ui::widgets::map::destroy(location_map_runtime_);
    location_map_created_ = false;
    clear_all_timers();

    if (top_bar_.back_btn)
    {
        ::ui::widgets::top_bar_set_back_callback(top_bar_, nullptr, nullptr);
    }

    container_ = nullptr;
    body_row_ = nullptr;
    right_column_ = nullptr;
    msg_list_ = nullptr;
    action_bar_ = nullptr;
    reply_btn_ = nullptr;
    compose_btn_ = nullptr;
    location_panel_ = nullptr;
    location_map_host_ = nullptr;
    location_map_visible_ = false;
}

void ChatConversationScreen::schedule_action_async(ActionIntent intent)
{
    if (!guard_ || !guard_->alive || !action_cb_)
    {
        return;
    }
    auto* payload = new ActionPayload();
    payload->guard = guard_;
    payload->action_cb = action_cb_;
    payload->user_data = action_cb_user_data_;
    payload->intent = intent;
    guard_->pending_async++;
    lv_async_call(async_action_cb, payload);
}

void ChatConversationScreen::schedule_message_action_async(
    MessageActionIntent intent,
    ::ui::chat::MessageRef ref)
{
    if (!guard_ || !guard_->alive || !message_action_cb_)
    {
        return;
    }
    auto* payload = new MessageActionPayload();
    payload->guard = guard_;
    payload->message_action_cb = message_action_cb_;
    payload->user_data = message_action_cb_user_data_;
    payload->intent = intent;
    payload->ref = ref;
    guard_->pending_async++;
    lv_async_call(async_message_action_cb, payload);
}

void ChatConversationScreen::schedule_back_async()
{
    if (!guard_ || !guard_->alive || !back_cb_)
    {
        return;
    }
    auto* payload = new BackPayload();
    payload->guard = guard_;
    payload->back_cb = back_cb_;
    payload->user_data = back_cb_user_data_;
    guard_->pending_async++;
    lv_async_call(async_back_cb, payload);
}

} // namespace ui
} // namespace chat

#endif
