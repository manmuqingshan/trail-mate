/**
 * @file ui_controller.cpp
 * @brief Chat UI controller implementation
 */

#include "ui/screens/chat/chat_ui_controller.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/usecase/contact_service.h"
#include "chat_presentation_adapters/chat_conversation_mapper.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "sys/event_bus.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/chat/chat_protocol_support.h"
#include "ui/screens/chat/chat_team_workflow.h"
#include "ui/ui_common.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui_chat_runtime/chat_delivery_action_port_adapter.h"
#include "ui_lvgl_ux_packs/common/key_verification_modal_renderer.h"
#include "ui_lvgl_ux_packs/common/team_position_picker_renderer.h"
#include "ui_presentation/key_verification/key_verification_model.h"
#include "ui_presentation/map/map_overlay_snapshot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifndef CHAT_UI_LOG_ENABLE
#define CHAT_UI_LOG_ENABLE 0
#endif

#if CHAT_UI_LOG_ENABLE
#define CHAT_UI_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_UI_LOG(...)
#endif

namespace chat
{
namespace ui
{

namespace
{
namespace chat_support = chat::ui::support;
namespace rtdir = ::platform::ui::reticulum_directory;

enum class ConversationScrollAnchor
{
    Top,
    Bottom
};

constexpr uint8_t kTeamChatChannelRaw = static_cast<uint8_t>(chat::ChannelId::TEAM);
constexpr chat::ChannelId kTeamChatChannel =
    static_cast<chat::ChannelId>(kTeamChatChannelRaw);

const char* protocol_short_label(chat::MeshProtocol protocol)
{
    return chat::infra::meshProtocolShortName(protocol);
}

const char* channel_display_name(chat::MeshProtocol protocol, chat::ChannelId channel)
{
    if (protocol == chat::MeshProtocol::Meshtastic)
    {
        return chat::meshtastic::channelName(app::configFacade().getConfig().meshtastic_config,
                                             channel);
    }
    if (protocol == chat::MeshProtocol::MeshCore)
    {
        const chat::MeshConfig& config = app::configFacade().getConfig().meshcore_config;
        const chat::MeshCoreChannelConfig& channel_config =
            config.meshCoreChannel(chat::meshCoreChannelSlotFromId(channel));
        if (channel_config.name[0] != '\0')
        {
            return channel_config.name;
        }
    }
    switch (channel)
    {
    case chat::ChannelId::SECONDARY:
        return "Secondary";
    case chat::ChannelId::PRIMARY:
    default:
        return "Primary";
    }
}

bool has_reticulum_destination(const chat::ConversationId& conv)
{
    return conv.protocol == chat::MeshProtocol::Reticulum &&
           chat::hasReticulumDestinationIdentity(conv.reticulum_identity);
}

std::string reticulum_destination_label(const chat::ReticulumPeerIdentity& identity)
{
    if (!chat::hasReticulumDestinationIdentity(identity))
    {
        return std::string();
    }

    char buf[9] = {};
    std::snprintf(buf,
                  sizeof(buf),
                  "%02X%02X%02X%02X",
                  static_cast<unsigned>(identity.destination_hash[0]),
                  static_cast<unsigned>(identity.destination_hash[1]),
                  static_cast<unsigned>(identity.destination_hash[2]),
                  static_cast<unsigned>(identity.destination_hash[3]));
    return buf;
}

std::string reticulum_contact_display_name(const chat::ConversationId& conv)
{
    if (!has_reticulum_destination(conv))
    {
        return std::string();
    }
    std::string name = app::messagingFacade()
                           .getContactService()
                           .getReticulumContactName(conv.reticulum_identity);
    if (!name.empty())
    {
        return name;
    }

    rtdir::LxmfAddressRecord record{};
    const auto status =
        rtdir::find_lxmf_address_by_destination(
            conv.reticulum_identity.destination_hash,
            &record);
    if (status.loaded && record.valid && record.display_name[0] != '\0')
    {
        return record.display_name;
    }
    return std::string();
}

bool same_conversation_party(const chat::ConversationId& lhs,
                             const chat::ConversationId& rhs)
{
    if (lhs.protocol != rhs.protocol)
    {
        return false;
    }

    const bool lhs_reticulum = has_reticulum_destination(lhs);
    const bool rhs_reticulum = has_reticulum_destination(rhs);
    if (lhs_reticulum || rhs_reticulum)
    {
        return lhs_reticulum && rhs_reticulum &&
               chat::sameReticulumDestinationHash(lhs.reticulum_identity,
                                                  rhs.reticulum_identity);
    }

    return lhs.peer == rhs.peer;
}

std::string base_conversation_name(const chat::ConversationId& conv)
{
    const std::string reticulum_name = reticulum_contact_display_name(conv);
    if (!reticulum_name.empty())
    {
        return reticulum_name;
    }

    if (has_reticulum_destination(conv))
    {
        return "Anonymous Peer";
    }

    if (conv.peer == 0)
    {
        return ::ui::i18n::tr("Broadcast");
    }

    std::string contact_name = app::messagingFacade().getContactService().getContactName(conv.peer);
    if (!contact_name.empty())
    {
        return contact_name;
    }

    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(conv.peer));
    return buf;
}

void format_reticulum_hash(const uint8_t* hash, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash)
    {
        std::snprintf(out, out_len, "--");
        return;
    }

    bool any = false;
    size_t used = 0;
    for (size_t index = 0;
         index < chat::kReticulumPeerHashSize && used + 2U < out_len;
         ++index)
    {
        any = any || hash[index] != 0;
        const int written = std::snprintf(out + used,
                                          out_len - used,
                                          "%02X",
                                          static_cast<unsigned>(hash[index]));
        if (written != 2)
        {
            break;
        }
        used += 2U;
    }
    out[used < out_len ? used : out_len - 1U] = '\0';
    if (!any)
    {
        std::snprintf(out, out_len, "--");
    }
}

const chat::ReticulumPeerIdentity& conversation_reticulum_identity(
    const chat::ConversationId& conv,
    const chat::contacts::NodeInfo* node)
{
    if (chat::hasReticulumDestinationIdentity(conv.reticulum_identity))
    {
        return conv.reticulum_identity;
    }
    if (node && chat::hasReticulumDestinationIdentity(node->reticulum_identity))
    {
        return node->reticulum_identity;
    }
    return conv.reticulum_identity;
}

std::string node_display_name_for_info(const chat::ConversationId& conv,
                                       const chat::contacts::NodeInfo* node)
{
    if (node)
    {
        if (!node->display_name.empty())
        {
            return node->display_name;
        }
        if (node->long_name[0] != '\0')
        {
            return node->long_name;
        }
        if (node->short_name[0] != '\0')
        {
            return node->short_name;
        }
    }
    return base_conversation_name(conv);
}

void apply_info_label(lv_obj_t* label, bool muted = false)
{
    if (!label)
    {
        return;
    }
    ::ui::components::two_pane_styles::init_once();
    if (muted)
    {
        ::ui::components::two_pane_styles::apply_label_muted(label);
    }
    else
    {
        ::ui::components::two_pane_styles::apply_label_primary(label);
    }
    ::ui::fonts::apply_localized_font(label,
                                      lv_label_get_text(label),
                                      ::ui::fonts::ui_chrome_font());
}

void add_info_row(lv_obj_t* parent, const char* label, const char* value)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(row,
                              lv_color_hex(::ui::components::two_pane_styles::kMainPanelBg),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row,
                                  lv_color_hex(::ui::components::two_pane_styles::kBorder),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(row, 1, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* key = lv_label_create(row);
    ::ui::i18n::set_label_text(key, label);
    apply_info_label(key, true);
    lv_obj_set_width(key, LV_PCT(100));
    lv_label_set_long_mode(key, LV_LABEL_LONG_DOT);

    lv_obj_t* val = lv_label_create(row);
    ::ui::i18n::set_label_text_raw(val, value && value[0] != '\0' ? value : "--");
    apply_info_label(val, false);
    lv_obj_set_width(val, LV_PCT(100));
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
}

lv_obj_t* create_info_modal_root(lv_obj_t* parent, int width, int height)
{
    lv_obj_t* root_parent = parent ? parent : lv_screen_active();
    if (!root_parent)
    {
        return nullptr;
    }

    lv_obj_t* bg = lv_obj_create(root_parent);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg,
                              lv_color_hex(::ui::components::two_pane_styles::kTextPrimary),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_foreground(bg);

    const auto resolved = ::ui::page_profile::resolve_modal_size(width, height, root_parent);
    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, resolved.width, resolved.height);
    lv_obj_center(win);
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(win,
                              lv_color_hex(::ui::components::two_pane_styles::kSidePanelBg),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win,
                                  lv_color_hex(::ui::components::two_pane_styles::kBorder),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, ::ui::page_profile::resolve_modal_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_row(win, 4, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    return bg;
}

chat::ConversationId teamConversationId()
{
    return chat::ConversationId(kTeamChatChannel, 0, chat_support::active_mesh_protocol());
}

bool isTeamConversationId(const chat::ConversationId& conv)
{
    return conv.channel == kTeamChatChannel && conv.peer == 0;
}

chat::ConversationMeta conversationMetaFromSnapshotRow(
    const ::ui::chat::ConversationRow& row)
{
    chat::ConversationMeta meta;
    if (!chat_presentation_adapters::toCoreConversationId(row.id, meta.id))
    {
        return meta;
    }
    meta.name = row.title.c_str();
    meta.preview = row.subtitle.c_str();
    meta.unread = static_cast<int>(row.unread_count);
    meta.last_timestamp = row.last_timestamp;
    return meta;
}

void appendSnapshotConversationsToControllerList(
    const ::ui::chat::ChatWorkspaceSnapshot& snapshot,
    std::vector<chat::ConversationMeta>& out)
{
    for (size_t i = 0; i < snapshot.conversation_count; ++i)
    {
        chat::ConversationId core_id;
        if (!chat_presentation_adapters::toCoreConversationId(snapshot.conversations[i].id, core_id))
        {
            continue;
        }
        chat::ConversationMeta meta = conversationMetaFromSnapshotRow(snapshot.conversations[i]);
        meta.id = core_id;
        out.push_back(meta);
    }
}

bool teamConversationMetaFromSnapshot(
    const ::ui::chat::ChatWorkspaceSnapshot& snapshot,
    chat::ConversationMeta& out)
{
    if (!snapshot.header.valid)
    {
        return false;
    }

    for (size_t i = 0; i < snapshot.conversation_count; ++i)
    {
        const auto& row = snapshot.conversations[i];
        if (row.id.kind != ::ui::chat::ConversationKind::Team)
        {
            continue;
        }

        out = chat::ConversationMeta{};
        out.id = teamConversationId();
        out.name = row.title.c_str();
        out.preview = row.subtitle.c_str();
        out.unread = static_cast<int>(row.unread_count);
        out.last_timestamp = row.last_timestamp;
        return true;
    }

    return false;
}

::ui::map::MapOverlaySnapshot buildConversationLocationOverlay(
    const ::ui::chat::ChatWorkspaceSnapshot& snapshot)
{
    ::ui::map::MapOverlaySnapshot overlay{};
    overlay.header.valid = true;
    overlay.header.version = 1;

    for (size_t i = 0; i < snapshot.location_participant_count; ++i)
    {
        if (overlay.item_count >= ::ui::map::MapOverlaySnapshot::kMaxItems)
        {
            overlay.truncated = true;
            break;
        }

        const auto& participant = snapshot.location_participants[i];
        if (!participant.valid)
        {
            continue;
        }

        auto& item = overlay.items[overlay.item_count++];
        item.kind = participant.self
                        ? ::ui::map::MapOverlayKind::CurrentPosition
                        : ::ui::map::MapOverlayKind::TeamMember;
        item.style = participant.self
                         ? ::ui::map::MapOverlayStyle::OwnPosition
                         : ::ui::map::MapOverlayStyle::Team;
        item.point.valid = true;
        item.point.lat = participant.lat;
        item.point.lon = participant.lon;
        item.stable_id = participant.node_id;
        item.visible = true;
        ::ui::copyText(item.label, participant.label.c_str());
    }

    overlay.truncated =
        overlay.truncated || snapshot.location_participants_truncated;
    return overlay;
}

void applySnapshotMessagesToConversation(
    const ::ui::chat::ChatWorkspaceSnapshot& snapshot,
    ChatConversationScreen& conversation,
    ConversationScrollAnchor scroll_anchor = ConversationScrollAnchor::Bottom)
{
    conversation.clearMessages();
    for (size_t i = 0; i < snapshot.message_count; ++i)
    {
        conversation.addMessage(snapshot.messages[i]);
    }
    conversation.setHistoryPaging(snapshot.has_older_messages,
                                  snapshot.has_newer_messages,
                                  snapshot.message_offset,
                                  snapshot.message_total_count);
    conversation.setLocationOverlay(buildConversationLocationOverlay(snapshot));
    if (scroll_anchor == ConversationScrollAnchor::Top)
    {
        conversation.scrollToTop();
    }
    else
    {
        conversation.scrollToBottom();
    }
}

const char* key_verification_action_failure_message(::ui::UiActionResult result)
{
    if (result.failure == ::ui::UiActionFailure::NotReady)
    {
        return "Key verification unavailable";
    }
    if (result.failure == ::ui::UiActionFailure::Unsupported)
    {
        return "Key verification unsupported";
    }
    if (result.failure == ::ui::UiActionFailure::InvalidInput)
    {
        return "Key verification unavailable";
    }
    return "Key verification failed";
}

const char* local_text_send_failure_message(::ui::UiActionResult result)
{
    switch (result.failure)
    {
    case ::ui::UiActionFailure::ChannelKeyMissing:
        return "Channel key missing";
    case ::ui::UiActionFailure::PeerKeyMissing:
        return "Peer key missing";
    case ::ui::UiActionFailure::TxDisabled:
        return "TX disabled";
    case ::ui::UiActionFailure::RadioOffline:
        return "Radio offline";
    case ::ui::UiActionFailure::DutyCycleLimited:
        return "TX rate limited";
    case ::ui::UiActionFailure::RadioTxFailed:
        return "Radio TX failed";
    case ::ui::UiActionFailure::LocalIdentityMissing:
        return "Identity missing";
    case ::ui::UiActionFailure::Busy:
        return "Radio busy";
    case ::ui::UiActionFailure::Unsupported:
        return "Conversation unsupported";
    case ::ui::UiActionFailure::InvalidInput:
        return "Message unavailable";
    case ::ui::UiActionFailure::NotReady:
        return "Radio not ready";
    case ::ui::UiActionFailure::Rejected:
    case ::ui::UiActionFailure::StorageError:
    case ::ui::UiActionFailure::None:
    default:
        return "Send failed";
    }
}

const char* delivery_retry_failure_message(
    ::chat::delivery::ChatDeliveryActionFailure failure)
{
    switch (failure)
    {
    case ::chat::delivery::ChatDeliveryActionFailure::InvalidRef:
    case ::chat::delivery::ChatDeliveryActionFailure::NotFound:
        return "Message unavailable";
    case ::chat::delivery::ChatDeliveryActionFailure::Unsupported:
        return "Retry unavailable";
    case ::chat::delivery::ChatDeliveryActionFailure::NotRetryable:
        return "Message not retryable";
    case ::chat::delivery::ChatDeliveryActionFailure::Rejected:
        return "Retry failed";
    case ::chat::delivery::ChatDeliveryActionFailure::None:
        return "Retry failed";
    }
    return "Retry failed";
}

void handle_message_list_action(chat::ui::ChatMessageListScreen::ActionIntent intent,
                                const chat::ConversationId& conv,
                                void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (!controller)
    {
        return;
    }
    controller->handleMessageListAction(intent, conv);
}

void handle_conversation_action(chat::ui::ChatConversationScreen::ActionIntent intent, void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->handleConversationAction(intent);
    }
}

void handle_conversation_message_action(
    chat::ui::ChatConversationScreen::MessageActionIntent intent,
    ::ui::chat::MessageRef ref,
    void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->handleConversationMessageAction(intent, ref);
    }
}

void handle_compose_back(void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->handleComposeAction(chat::ui::ChatComposeScreen::ActionIntent::Cancel);
    }
}

void handle_compose_action(chat::ui::ChatComposeScreen::ActionIntent intent, void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->handleComposeAction(intent);
    }
}

void handle_conversation_back(void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->backToList();
    }
}
} // namespace

UiController::UiController(lv_obj_t* parent,
                           chat::ChatService& service,
                           ::ui::chat::ChatWorkspaceModel& chat_model,
                           ChatTeamWorkflow& team_workflow,
                           ::ui::key_verification::KeyVerificationModel*
                               key_verification_model,
                           ::ui_chat_runtime::ChatDeliveryActionPortAdapter*
                               delivery_action_adapter,
                           chat::ChannelId initial_channel,
                           ExitRequestCallback exit_request,
                           void* exit_request_user_data)
    : parent_(parent), service_(service), chat_model_(chat_model),
      team_workflow_(team_workflow),
      key_verification_model_(key_verification_model),
      delivery_action_adapter_(delivery_action_adapter),
      state_(State::ChannelList),
      current_channel_(initial_channel),
      current_conv_(chat::ConversationId(initial_channel, 0, chat_support::active_mesh_protocol())),
      exit_request_(exit_request), exit_request_user_data_(exit_request_user_data)
{
}

UiController::~UiController()
{
    closeTeamPositionPicker(false);
    team_position_picker_.reset();
    closeKeyVerificationModal(false);
    closeConversationInfoModal(false);
    stopTeamConversationTimer();
    service_.setModelEnabled(false);
    channel_list_.reset();
    conversation_.reset();
    cleanupComposeIme();
    compose_.reset();
}

void UiController::cleanupComposeIme()
{
    if (compose_ime_)
    {
        compose_ime_->detach();
        compose_ime_.reset();
    }
}

void UiController::init()
{
    TeamPositionPickerRenderer::Callbacks callbacks;
    callbacks.on_icon_selected = [](void* user_data, uint8_t icon_id)
    {
        auto* controller = static_cast<UiController*>(user_data);
        if (controller)
        {
            controller->onTeamPositionIconSelected(icon_id);
        }
    };
    callbacks.on_cancel = [](void* user_data)
    {
        auto* controller = static_cast<UiController*>(user_data);
        if (controller)
        {
            controller->onTeamPositionCancel();
        }
    };
    callbacks.user_data = this;
    team_position_picker_.reset(
        new TeamPositionPickerRenderer(parent_, callbacks));
    switchToChannelList();
}

void UiController::update()
{
    // Refresh UI only when an event marks the conversation list dirty.
    refreshUnreadCounts(false);
}

void UiController::onChannelClicked(chat::ConversationId conv)
{
    if (channel_list_)
    {
        handleChannelSelected(conv);
    }
}

void UiController::handleMessageListAction(
    ChatMessageListScreen::ActionIntent intent,
    const chat::ConversationId& conv)
{
    switch (intent)
    {
    case ChatMessageListScreen::ActionIntent::SelectConversation:
        onChannelClicked(conv);
        break;
    case ChatMessageListScreen::ActionIntent::ShowInfo:
        openConversationInfoModal(conv);
        break;
    case ChatMessageListScreen::ActionIntent::DeleteConversation:
        handleDeleteConversation(conv);
        break;
    case ChatMessageListScreen::ActionIntent::Back:
    default:
        exitToMenu();
        break;
    }
}

void UiController::backToList()
{
    switchToChannelList();
}

void UiController::onInput(const sys::InputEvent& event)
{
    switch (state_)
    {
    case State::ChannelList:
        if (event.input_type == sys::InputEvent::RotaryTurn)
        {
            // Handle rotary navigation
            // (Implementation depends on rotary event details)
        }
        else if (event.input_type == sys::InputEvent::RotaryPress)
        {
            if (channel_list_)
            {
                (void)channel_list_->openSelectedActionMenu();
            }
        }
        else if (event.input_type == sys::InputEvent::KeyPress && event.value == 27)
        {
            // ESC - return to main menu (handled by parent)
        }
        break;

    case State::Conversation:
        if (event.input_type == sys::InputEvent::KeyPress && event.value == 27)
        {
            // ESC - return to channel list
            switchToChannelList();
        }
        break;

    case State::Compose:
        if (event.input_type == sys::InputEvent::KeyPress && event.value == 27)
        {
            // ESC - cancel compose
            switchToConversation(current_conv_);
        }
        break;

    default:
        break;
    }
}

void UiController::onRuntimeMessageArrived(chat::MessageId msg_id)
{
    CHAT_UI_LOG("[UiController::onRuntimeMessageArrived] msg_id=%lu, state=%d, current_channel=%d\n",
                static_cast<unsigned long>(msg_id), (int)state_, (int)current_channel_);

    const ChatMessage* latest = service_.getMessage(msg_id);
    if (latest)
    {
        const chat::ConversationId latest_conv = chat::conversationIdForMessage(*latest);
        const bool is_current_conversation =
            (state_ == State::Conversation) &&
            (current_conv_ == latest_conv);
        updateConversationMetaForMessage(*latest, !is_current_conversation);
        if (is_current_conversation)
        {
            (void)updateConversationViewForIncoming(*latest);
            reloadConversationView();
            (void)chat_model_.markRead(
                chat_presentation_adapters::toUiConversationId(current_conv_));
        }
        else
        {
            conversation_list_dirty_ = true;
        }
    }
    refreshUnreadCounts(false);
}

void UiController::onRuntimeSendResult(chat::MessageId msg_id)
{
    if (state_ == State::Conversation && conversation_)
    {
        const ChatMessage* msg = service_.getMessage(msg_id);
        if (!msg || !conversation_->updateMessageStatus(msg_id, msg->status))
        {
            reloadConversationView();
        }
    }
}

void UiController::onRuntimeUnreadChanged()
{
    conversation_list_dirty_ = true;
    refreshUnreadCounts(false);
}

void UiController::showKeyVerification(
    const ::ui::key_verification::KeyVerificationSnapshot& snapshot)
{
    renderKeyVerificationModal(snapshot);
}

void UiController::switchToChannelList()
{
    closeConversationInfoModal(true);
    closeTeamPositionPicker(true);
    state_ = State::ChannelList;
    stopTeamConversationTimer();
    team_conv_active_ = false;
    CHAT_UI_LOG("[UiController] switchToChannelList: parent=%p active=%p sleeping=%d\n",
                parent_, lv_screen_active(), platform::ui::screen::is_sleeping() ? 1 : 0);
    if (lv_obj_t* active = lv_screen_active())
    {
        CHAT_UI_LOG("[UiController] switchToChannelList active child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
        CHAT_UI_LOG("[UiController] switchToChannelList parent child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(parent_));
    }

    if (conversation_)
    {
        conversation_.reset();
    }
    if (compose_)
    {
        cleanupComposeIme();
        compose_.reset();
    }

    if (!channel_list_)
    {
        channel_list_.reset(new ChatMessageListScreen(parent_));
        channel_list_->setActionCallback(handle_message_list_action, this);
    }

    service_.setModelEnabled(true);
    refreshUnreadCounts(false);
}

void UiController::switchToConversation(chat::ConversationId conv)
{
    closeConversationInfoModal(true);
    closeTeamPositionPicker(true);
    state_ = State::Conversation;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    team_conv_active_ = isTeamConversation(conv);
    stopTeamConversationTimer();
    CHAT_UI_LOG("[UiController] switchToConversation: parent=%p active=%p sleeping=%d conv_peer=%08lX\n",
                parent_, lv_screen_active(), platform::ui::screen::is_sleeping() ? 1 : 0,
                (unsigned long)conv.peer);
    if (lv_obj_t* active = lv_screen_active())
    {
        CHAT_UI_LOG("[UiController] switchToConversation active child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
        CHAT_UI_LOG("[UiController] switchToConversation parent child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(parent_));
    }

    if (channel_list_)
    {
        channel_list_.reset();
    }
    if (compose_)
    {
        cleanupComposeIme();
        compose_.reset();
    }

#if defined(ARDUINO_T_WATCH_S3)
    if (!team_conv_active_ && conv.peer == 0 && conv.channel == chat::ChannelId::PRIMARY)
    {
        auto recent = service_.getRecentMessages(conv, 1);
        if (recent.empty())
        {
            switchToCompose(conv);
            return;
        }
    }
#endif

    if (!conversation_)
    {
        conversation_.reset(new ChatConversationScreen(parent_, conv));
        conversation_->setActionCallback(handle_conversation_action, this);
        conversation_->setMessageActionCallback(handle_conversation_message_action, this);
        conversation_->setBackCallback(handle_conversation_back, this);
    }
    else
    {
        conversation_->setMessageActionCallback(handle_conversation_message_action, this);
    }
    const bool can_reply = team_conv_active_
                               ? chat_support::supports_team_chat()
                               : (conv.protocol == chat_support::active_mesh_protocol() &&
                                  chat_support::supports_local_text_chat());
    conversation_->setReplyEnabled(can_reply);

    if (team_conv_active_)
    {
        const bool loaded =
            team_workflow_.buildSelectedSnapshot(team_chat_snapshot_buffer_);
        std::string title = loaded && team_chat_snapshot_buffer_.conversation_count > 0
                                ? team_chat_snapshot_buffer_.conversations[0].title.c_str()
                                : "Team";
        const uint16_t unread = loaded && team_chat_snapshot_buffer_.conversation_count > 0
                                    ? team_chat_snapshot_buffer_.conversations[0].unread_count
                                    : 0;
        conversation_->setHeaderText(title.c_str(), nullptr);
        conversation_->updateBatteryFromBoard();
        if (loaded)
        {
            applySnapshotMessagesToConversation(team_chat_snapshot_buffer_, *conversation_);
        }
        startTeamConversationTimer();
        if (loaded && unread != 0)
        {
            (void)team_workflow_.markRead(
                team_chat_snapshot_buffer_.conversations[0].id);
            sys::EventBus::publish(
                new sys::ChatUnreadChangedEvent(kTeamChatChannelRaw, 0), 0);
        }
        return;
    }

    const ::ui::chat::ConversationId ui_conv =
        chat_presentation_adapters::toUiConversationId(conv);
    (void)chat_model_.selectConversation(ui_conv);

    // Update header (prefer contact name, else short_name)
    std::string title = resolveConversationDisplayName(conv);
    if (conv.peer != 0)
    {
        std::string contact_name = app::messagingFacade().getContactService().getContactName(conv.peer);
        if (!contact_name.empty())
        {
            title = contact_name;
        }
        else
        {
            size_t total = 0;
            auto convs = service_.getConversations(0, 0, &total);
            for (const auto& c : convs)
            {
                if (c.id == conv)
                {
                    title = c.name;
                    break;
                }
            }
        }
    }
    conversation_->updateBatteryFromBoard();
    std::string header = "[" + std::string(protocol_short_label(conv.protocol)) + "] " + title;
    conversation_->setHeaderText(header.c_str(), nullptr);

    if (loadChatSnapshot())
    {
        applySnapshotMessagesToConversation(chat_snapshot_buffer_, *conversation_);
    }
    (void)chat_model_.markRead(ui_conv);
}

void UiController::switchToCompose(chat::ConversationId conv)
{
    closeConversationInfoModal(true);
    closeTeamPositionPicker(true);
    const bool is_team_conv = isTeamConversation(conv);
    if (!is_team_conv && conv.protocol != chat_support::active_mesh_protocol())
    {
        ::ui::feedback::show_notice("Conversation protocol mismatch", 2000);
        return;
    }
    if (!is_team_conv && !chat_support::supports_local_text_chat())
    {
        ::ui::feedback::show_notice(chat_support::local_text_chat_unavailable_message(), 2200);
        return;
    }
    if (is_team_conv && !chat_support::supports_team_chat())
    {
        ::ui::feedback::show_notice(chat_support::team_chat_unavailable_message(), 2200);
        return;
    }

    state_ = State::Compose;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    team_conv_active_ = is_team_conv;

    if (!is_team_conv)
    {
        (void)chat_model_.selectConversation(
            chat_presentation_adapters::toUiConversationId(conv));
    }

    stopTeamConversationTimer();
    CHAT_UI_LOG("[UiController] switchToCompose: parent=%p active=%p sleeping=%d conv_peer=%08lX\n",
                parent_, lv_screen_active(), platform::ui::screen::is_sleeping() ? 1 : 0,
                (unsigned long)conv.peer);
    if (lv_obj_t* active = lv_screen_active())
    {
        CHAT_UI_LOG("[UiController] switchToCompose active child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
        CHAT_UI_LOG("[UiController] switchToCompose parent child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(parent_));
    }

    if (channel_list_)
    {
        channel_list_.reset();
    }
    if (conversation_)
    {
        conversation_.reset();
    }

    if (!compose_)
    {
        compose_.reset(new ChatComposeScreen(parent_, conv));
        compose_->setActionCallback(handle_compose_action, this);
        compose_->setBackCallback(handle_compose_back, this);
    }

    lv_obj_t* compose_content = compose_->getContent();
    lv_obj_t* compose_textarea = compose_->getTextarea();
    if (compose_content && compose_textarea)
    {
        if (compose_ime_)
        {
            compose_ime_->detach();
        }
        else
        {
            compose_ime_.reset(new ::ui::widgets::ImeWidget());
        }
        compose_ime_->init(compose_content, compose_textarea);
        compose_->attachImeWidget(compose_ime_.get());
        if (lv_group_t* group = lv_group_get_default())
        {
            lv_group_add_obj(group, compose_ime_->focus_obj());
        }
    }

    std::string title = resolveConversationDisplayName(conv);
    if (team_conv_active_)
    {
        if (team_workflow_.buildSelectedSnapshot(team_chat_snapshot_buffer_) &&
            team_chat_snapshot_buffer_.conversation_count > 0)
        {
            title = team_chat_snapshot_buffer_.conversations[0].title.c_str();
        }
        else
        {
            title = "Team";
        }
        compose_->setHeaderText(title.c_str(), nullptr);
        compose_->setActionLabels("Send", "Cancel");
        compose_->setPositionButton("Position", true);
        return;
    }

    if (conv.peer != 0)
    {
        std::string contact_name = app::messagingFacade().getContactService().getContactName(conv.peer);
        if (!contact_name.empty())
        {
            title = contact_name;
        }
        else
        {
            size_t total = 0;
            auto convs = service_.getConversations(0, 0, &total);
            for (const auto& c : convs)
            {
                if (c.id == conv)
                {
                    title = c.name;
                    break;
                }
            }
        }
    }
    std::string header = "[" + std::string(protocol_short_label(conv.protocol)) + "] " + title;
    compose_->setHeaderText(header.c_str(), nullptr);
    compose_->setPositionButton(nullptr, false);
}

void UiController::handleChannelSelected(const chat::ConversationId& conv)
{
    switchToConversation(conv);
}

void UiController::handleDeleteConversation(const chat::ConversationId& conv)
{
    closeConversationInfoModal(true);
    service_.clearConversation(conv);
    conversation_list_dirty_ = true;
    syncConversationListFromStore();
    applyConversationListToUi();
    ::ui::feedback::show_notice("Chat deleted", 1400);
}

void UiController::prepareConversationInfoGroup()
{
    if (!conversation_info_group_)
    {
        conversation_info_group_ = lv_group_create();
    }
    lv_group_remove_all_objs(conversation_info_group_);
    conversation_info_prev_group_ = lv_group_get_default();
    set_default_group(conversation_info_group_);
}

void UiController::restoreConversationInfoGroup()
{
    if (conversation_info_prev_group_)
    {
        set_default_group(conversation_info_prev_group_);
    }
    conversation_info_prev_group_ = nullptr;
}

void UiController::closeConversationInfoModal(bool restore_group)
{
    if (conversation_info_modal_)
    {
        lv_obj_del(conversation_info_modal_);
        conversation_info_modal_ = nullptr;
    }
    if (restore_group)
    {
        restoreConversationInfoGroup();
    }
    else if (conversation_info_prev_group_)
    {
        set_default_group(conversation_info_prev_group_);
        conversation_info_prev_group_ = nullptr;
    }
    if (conversation_info_group_ && !restore_group)
    {
        lv_group_del(conversation_info_group_);
        conversation_info_group_ = nullptr;
        conversation_info_prev_group_ = nullptr;
    }
}

void UiController::conversation_info_close_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller)
    {
        return;
    }
    controller->closeConversationInfoModal(true);
}

void UiController::conversation_info_key_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller || lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        controller->closeConversationInfoModal(true);
        lv_event_stop_processing(e);
    }
}

void UiController::openConversationInfoModal(const chat::ConversationId& conv)
{
    if (conversation_info_modal_)
    {
        closeConversationInfoModal(true);
    }

    auto& contact_service = app::messagingFacade().getContactService();
    uint32_t node_id = conv.peer;
    const chat::contacts::NodeInfo* node = nullptr;
    if (has_reticulum_destination(conv))
    {
        uint32_t resolved_node_id = 0;
        if (contact_service.findNodeIdByReticulumDestinationHash(
                conv.reticulum_identity.destination_hash,
                &resolved_node_id))
        {
            node_id = resolved_node_id;
        }
    }
    if (node_id != 0)
    {
        node = contact_service.getNodeInfo(node_id);
    }

    const std::string display_name = node_display_name_for_info(conv, node);
    const chat::ReticulumPeerIdentity& identity =
        conversation_reticulum_identity(conv, node);

    char node_id_text[16] = {};
    std::snprintf(node_id_text,
                  sizeof(node_id_text),
                  "%08lX",
                  static_cast<unsigned long>(node_id));

    char peer_text[16] = {};
    std::snprintf(peer_text,
                  sizeof(peer_text),
                  "%08lX",
                  static_cast<unsigned long>(conv.peer));

    char destination_hash[chat::kReticulumPeerHashSize * 2U + 1U] = {};
    char identity_hash[chat::kReticulumPeerHashSize * 2U + 1U] = {};
    format_reticulum_hash(identity.destination_hash,
                          destination_hash,
                          sizeof(destination_hash));
    format_reticulum_hash(identity.identity_hash,
                          identity_hash,
                          sizeof(identity_hash));

    prepareConversationInfoGroup();
    conversation_info_modal_ = create_info_modal_root(parent_, 288, 214);
    lv_obj_t* win = conversation_info_modal_ ? lv_obj_get_child(conversation_info_modal_, 0) : nullptr;
    if (!win)
    {
        closeConversationInfoModal(true);
        return;
    }
    lv_obj_add_event_cb(conversation_info_modal_,
                        conversation_info_key_event_cb,
                        LV_EVENT_KEY,
                        this);

    lv_obj_t* title = lv_label_create(win);
    ::ui::i18n::set_label_text_raw(title, display_name.c_str());
    apply_info_label(title, false);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t* content = lv_obj_create(win);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, 3, LV_PART_MAIN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(content, conversation_info_key_event_cb, LV_EVENT_KEY, this);

    add_info_row(content, "Display Name", display_name.c_str());
    add_info_row(content, "Protocol", protocol_short_label(conv.protocol));
    if (has_reticulum_destination(conv) ||
        chat::hasReticulumDestinationIdentity(identity))
    {
        add_info_row(content, "LXMF Address", destination_hash);
        add_info_row(content, "Identity Hash", identity_hash);
        add_info_row(content, "Node ID", node_id != 0 ? node_id_text : "--");
        if (node && node->hops_away != 0xFF)
        {
            char hops[16] = {};
            std::snprintf(hops, sizeof(hops), "%u", static_cast<unsigned>(node->hops_away));
            add_info_row(content, "Hops", hops);
        }
    }
    else
    {
        add_info_row(content, "Peer", peer_text);
        add_info_row(content, "Node ID", node_id != 0 ? node_id_text : "--");
    }

    lv_obj_t* close_btn = lv_btn_create(win);
    lv_obj_set_size(close_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    ::ui::components::two_pane_styles::apply_btn_basic(close_btn);
    lv_obj_set_style_bg_color(close_btn,
                              lv_color_hex(::ui::components::two_pane_styles::kAccent),
                              LV_PART_MAIN);
    lv_obj_t* close_label = lv_label_create(close_btn);
    ::ui::i18n::set_label_text(close_label, "Close");
    apply_info_label(close_label, false);
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_btn,
                        conversation_info_close_event_cb,
                        LV_EVENT_CLICKED,
                        this);
    lv_obj_add_event_cb(close_btn,
                        conversation_info_key_event_cb,
                        LV_EVENT_KEY,
                        this);

    lv_group_add_obj(conversation_info_group_, content);
    lv_group_add_obj(conversation_info_group_, close_btn);
    lv_group_focus_obj(close_btn);
}

void UiController::handleSendMessage(const std::string& text)
{
    if (text.empty())
    {
        return;
    }
    if (team_conv_active_)
    {
        const ::ui::UiActionResult result =
            team_workflow_.sendText(text.c_str());
        ::ui::feedback::show_notice(
            result.ok ? "Sent" : team_workflow_.textSendFailureMessage(result),
            result.ok ? 1400 : 2000);
        handleComposeSendDone(result.ok, false);
        return;
    }
    if (!chat_support::supports_local_text_chat())
    {
        ::ui::feedback::show_notice(chat_support::local_text_chat_unavailable_message(), 2200);
        return;
    }

    const ::ui::UiActionResult result = chat_model_.sendMessage(text.c_str());
    if (!result.ok)
    {
        ::ui::feedback::show_notice(local_text_send_failure_message(result),
                                    2000);
    }
    handleComposeSendDone(result.ok, false);
}

void UiController::handleComposeSendDone(bool ok, bool timeout)
{
    (void)ok;
    (void)timeout;
    if (state_ == State::Compose)
    {
        switchToConversation(current_conv_);
    }
}

void UiController::refreshUnreadCounts()
{
    refreshUnreadCounts(true);
}

void UiController::refreshUnreadCounts(const bool force_reload)
{
    if (!channel_list_)
    {
        return;
    }

    if (force_reload || conversation_list_dirty_ || !conversation_list_loaded_)
    {
        syncConversationListFromStore();
    }
    applyConversationListToUi();
}

void UiController::syncConversationListFromStore()
{
    cached_conversations_.clear();
    if (loadChatSnapshot())
    {
        appendSnapshotConversationsToControllerList(chat_snapshot_buffer_,
                                                    cached_conversations_);
    }
    normalizeConversationNames(cached_conversations_);

    chat::ConversationMeta team_conv;
    if (loadTeamChatSnapshot() &&
        teamConversationMetaFromSnapshot(team_chat_snapshot_buffer_, team_conv))
    {
        cached_conversations_.insert(cached_conversations_.begin(), team_conv);
    }

    conversation_list_dirty_ = false;
    conversation_list_loaded_ = true;
}

void UiController::normalizeConversationNames(std::vector<chat::ConversationMeta>& convs) const
{
    for (auto& conv : convs)
    {
        if (isTeamConversationId(conv.id))
        {
            continue;
        }

        const std::string reticulum_name =
            reticulum_contact_display_name(conv.id);
        if (!reticulum_name.empty())
        {
            conv.name = reticulum_name;
        }
        else if (conv.name.empty())
        {
            conv.name = base_conversation_name(conv.id);
        }
    }

    for (auto& conv : convs)
    {
        if (isTeamConversationId(conv.id))
        {
            continue;
        }

        int channel_variant_count = 0;
        for (const auto& other : convs)
        {
            if (isTeamConversationId(other.id))
            {
                continue;
            }
            if (other.id.protocol != conv.id.protocol)
            {
                continue;
            }
            if (!same_conversation_party(other.id, conv.id))
            {
                continue;
            }
            channel_variant_count++;
        }

        if (channel_variant_count > 1)
        {
            conv.name += " (";
            conv.name += channel_display_name(conv.id.protocol, conv.id.channel);
            conv.name += ")";
        }
    }
}

void UiController::applyConversationListToUi()
{
    if (!channel_list_)
    {
        return;
    }

    channel_list_->setConversations(cached_conversations_);
    channel_list_->updateBatteryFromBoard();
}

std::string UiController::resolveConversationDisplayName(const chat::ConversationId& conv) const
{
    if (isTeamConversation(conv))
    {
        return "Team";
    }

    for (const auto& item : cached_conversations_)
    {
        if (item.id == conv && !item.name.empty())
        {
            const std::string reticulum_name =
                reticulum_contact_display_name(conv);
            return reticulum_name.empty() ? item.name : reticulum_name;
        }
    }

    return base_conversation_name(conv);
}

void UiController::updateConversationMetaForMessage(const chat::ChatMessage& msg,
                                                    const bool increment_unread)
{
    const chat::ConversationId message_conv = chat::conversationIdForMessage(msg);
    if (isTeamConversation(message_conv))
    {
        conversation_list_dirty_ = true;
        return;
    }

    chat::ConversationMeta meta;
    meta.id = message_conv;
    meta.name = base_conversation_name(meta.id);
    meta.preview = msg.text;
    meta.last_timestamp = msg.timestamp;
    meta.unread = (increment_unread && msg.status == chat::MessageStatus::Incoming) ? 1 : 0;
    meta.reticulum_identity = msg.reticulum_identity;

    bool found = false;
    for (auto it = cached_conversations_.begin(); it != cached_conversations_.end(); ++it)
    {
        if (!(it->id == meta.id))
        {
            continue;
        }
        found = true;
        meta.unread += it->unread;
        if (!increment_unread && msg.status == chat::MessageStatus::Incoming)
        {
            meta.unread = 0;
        }
        cached_conversations_.erase(it);
        break;
    }

    cached_conversations_.insert(cached_conversations_.begin(), meta);
    normalizeConversationNames(cached_conversations_);
}

bool UiController::updateConversationViewForIncoming(const chat::ChatMessage& msg)
{
    if (!conversation_)
    {
        return false;
    }

    if (!(current_conv_ == chat::conversationIdForMessage(msg)))
    {
        return false;
    }

    reloadConversationView();
    return true;
}

void UiController::reloadConversationView()
{
    if (!conversation_ || team_conv_active_)
    {
        return;
    }

    if (!loadChatSnapshot())
    {
        return;
    }
    applySnapshotMessagesToConversation(chat_snapshot_buffer_, *conversation_);
}

bool UiController::isTeamConversation(const chat::ConversationId& conv) const
{
    return isTeamConversationId(conv);
}

bool UiController::loadChatSnapshot()
{
    return chat_model_.buildSnapshot(chat_snapshot_buffer_);
}

bool UiController::loadTeamChatSnapshot()
{
    return team_workflow_.buildSnapshot(team_chat_snapshot_buffer_);
}

void UiController::refreshTeamConversation()
{
    if (!conversation_ || !team_conv_active_)
    {
        return;
    }

    if (loadTeamChatSnapshot())
    {
        applySnapshotMessagesToConversation(team_chat_snapshot_buffer_, *conversation_);
    }
}

void UiController::startTeamConversationTimer()
{
    if (team_conv_timer_)
    {
        lv_timer_resume(team_conv_timer_);
        return;
    }
    team_conv_timer_ = lv_timer_create([](lv_timer_t* timer)
                                       {
        auto* controller = static_cast<UiController*>(lv_timer_get_user_data(timer));
        if (controller)
        {
            controller->refreshTeamConversation();
        } },
                                       1000, this);
    if (team_conv_timer_)
    {
        lv_timer_set_repeat_count(team_conv_timer_, -1);
    }
}

void UiController::stopTeamConversationTimer()
{
    if (!team_conv_timer_)
    {
        return;
    }
    lv_timer_del(team_conv_timer_);
    team_conv_timer_ = nullptr;
}

bool UiController::isTeamPositionPickerOpen() const
{
    return team_position_picker_ && team_position_picker_->isOpen();
}

void UiController::updateTeamPositionPickerHint(uint8_t icon_id)
{
    if (team_position_picker_)
    {
        team_position_picker_->updateHint(icon_id);
    }
}

void UiController::openTeamPositionPicker()
{
    if (!team_conv_active_ || !compose_ || !parent_)
    {
        return;
    }
    if (!team_position_picker_)
    {
        return;
    }
    (void)team_position_picker_->open();
}

void UiController::closeTeamPositionPicker(bool restore_group)
{
    if (team_position_picker_)
    {
        team_position_picker_->close(restore_group);
    }
}

bool UiController::isKeyVerificationModalOpen() const
{
    return key_verify_modal_.overlay != nullptr;
}

void UiController::clearKeyVerificationError()
{
    chat::ui::clearKeyVerificationError(key_verify_modal_);
}

void UiController::key_verify_submit_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY)
    {
        controller->submitKeyVerificationInput();
    }
}

void UiController::key_verify_close_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER && key != LV_KEY_ESC && key != LV_KEY_BACKSPACE)
        {
            return;
        }
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY)
    {
        controller->closeKeyVerificationModal(true);
    }
}

void UiController::key_verify_trust_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY)
    {
        controller->trustKeyFromVerificationModal();
    }
}

void UiController::destroyKeyVerificationModal(bool restore_group)
{
    chat::ui::destroyKeyVerificationModal(
        key_verify_modal_,
        key_verify_ime_,
        restore_group);
}

void UiController::renderKeyVerificationModal(
    const ::ui::key_verification::KeyVerificationSnapshot& snapshot)
{
    KeyVerificationModalCallbacks callbacks;
    callbacks.submit = key_verify_submit_event_cb;
    callbacks.close = key_verify_close_event_cb;
    callbacks.trust = key_verify_trust_event_cb;
    callbacks.user_data = this;
    chat::ui::renderKeyVerificationModal(
        parent_,
        snapshot,
        callbacks,
        key_verify_modal_,
        key_verify_ime_);
}

void UiController::closeKeyVerificationModal(bool restore_group)
{
    destroyKeyVerificationModal(restore_group);
    if (key_verification_model_ != nullptr)
    {
        key_verification_model_->clearSelection();
    }
}

void UiController::submitKeyVerificationInput()
{
    if (!key_verify_modal_.textarea || key_verification_model_ == nullptr)
    {
        return;
    }

    clearKeyVerificationError();
    const char* text = lv_textarea_get_text(key_verify_modal_.textarea);
    if (!text || text[0] == '\0')
    {
        if (key_verify_modal_.error_label)
        {
            ::ui::i18n::set_label_text(
                key_verify_modal_.error_label,
                "Enter the 6-digit number");
        }
        return;
    }

    char* end_ptr = nullptr;
    unsigned long parsed = std::strtoul(text, &end_ptr, 10);
    if (!end_ptr || *end_ptr != '\0' || parsed > 999999UL)
    {
        if (key_verify_modal_.error_label)
        {
            ::ui::i18n::set_label_text(
                key_verify_modal_.error_label,
                "Invalid number");
        }
        return;
    }

    const auto result = key_verification_model_->submitNumber(
        static_cast<uint32_t>(parsed));
    if (!result.ok)
    {
        if (key_verify_modal_.error_label)
        {
            ::ui::i18n::set_label_text_raw(
                key_verify_modal_.error_label,
                key_verification_action_failure_message(result));
        }
        return;
    }

    ::ui::feedback::show_notice("Verification number sent", 2000);
    closeKeyVerificationModal(true);
}

void UiController::trustKeyFromVerificationModal()
{
    if (key_verification_model_ == nullptr)
    {
        closeKeyVerificationModal(true);
        return;
    }

    const auto result = key_verification_model_->accept();
    ::ui::feedback::show_notice(
        result.ok ? "Key marked trusted"
                  : key_verification_action_failure_message(result),
        2000);
    closeKeyVerificationModal(true);
}

void UiController::onTeamPositionCancel()
{
    closeTeamPositionPicker(true);
}

void UiController::onTeamPositionIconSelected(uint8_t icon_id)
{
    closeTeamPositionPicker(true);
    const auto result = team_workflow_.sendCurrentLocationMarker(icon_id);
    if (!result.ok)
    {
        ::ui::feedback::show_notice(
            team_workflow_.locationSendFailureMessage(result),
            2000);
    }
    switchToConversation(current_conv_);
}

void UiController::handleConversationAction(ChatConversationScreen::ActionIntent intent)
{
    if (intent == ChatConversationScreen::ActionIntent::LoadOlder)
    {
        if (team_conv_active_ || !conversation_)
        {
            return;
        }
        if (!loadChatSnapshot())
        {
            conversation_->setHistoryPaging(false, false, 0, 0);
            ::ui::feedback::show_notice("No more messages", 1400);
            return;
        }
        if (!chat_snapshot_buffer_.has_older_messages)
        {
            conversation_->setHistoryPaging(false,
                                            chat_snapshot_buffer_.has_newer_messages,
                                            chat_snapshot_buffer_.message_offset,
                                            chat_snapshot_buffer_.message_total_count);
            ::ui::feedback::show_notice("No more messages", 1400);
            return;
        }
        const uint16_t page_size =
            ::ui::chat::ChatWorkspaceSnapshot::kMaxMessages;
        const uint16_t total_count = chat_snapshot_buffer_.message_total_count;
        if (total_count <= page_size)
        {
            conversation_->setHistoryPaging(false,
                                            chat_snapshot_buffer_.has_newer_messages,
                                            chat_snapshot_buffer_.message_offset,
                                            total_count);
            ::ui::feedback::show_notice("No more messages", 1400);
            return;
        }
        const uint16_t current_offset = chat_snapshot_buffer_.message_offset;
        const uint16_t max_offset =
            static_cast<uint16_t>(total_count - page_size);
        const uint32_t requested_offset =
            static_cast<uint32_t>(current_offset) +
            page_size;
        const uint16_t next_offset =
            requested_offset > max_offset
                ? max_offset
                : static_cast<uint16_t>(requested_offset);
        if (next_offset == current_offset)
        {
            conversation_->setHistoryPaging(false,
                                            chat_snapshot_buffer_.has_newer_messages,
                                            current_offset,
                                            total_count);
            ::ui::feedback::show_notice("No more messages", 1400);
            return;
        }
        chat_model_.setMessageOffset(next_offset);
        if (!loadChatSnapshot() || chat_snapshot_buffer_.message_count == 0)
        {
            chat_model_.setMessageOffset(current_offset);
            (void)loadChatSnapshot();
            conversation_->setHistoryPaging(false,
                                            chat_snapshot_buffer_.has_newer_messages,
                                            current_offset,
                                            chat_snapshot_buffer_.message_total_count);
            ::ui::feedback::show_notice("No more messages", 1400);
            return;
        }
        applySnapshotMessagesToConversation(chat_snapshot_buffer_, *conversation_);
        return;
    }

    if (intent == ChatConversationScreen::ActionIntent::LoadNewer)
    {
        if (team_conv_active_ || !conversation_)
        {
            return;
        }
        if (!loadChatSnapshot())
        {
            conversation_->setHistoryPaging(false, false, 0, 0);
            ::ui::feedback::show_notice("Latest messages", 1400);
            return;
        }
        if (!chat_snapshot_buffer_.has_newer_messages)
        {
            conversation_->setHistoryPaging(chat_snapshot_buffer_.has_older_messages,
                                            false,
                                            chat_snapshot_buffer_.message_offset,
                                            chat_snapshot_buffer_.message_total_count);
            ::ui::feedback::show_notice("Latest messages", 1400);
            return;
        }
        const uint16_t page_size =
            ::ui::chat::ChatWorkspaceSnapshot::kMaxMessages;
        const uint16_t current_offset = chat_snapshot_buffer_.message_offset;
        const uint16_t next_offset =
            current_offset > page_size
                ? static_cast<uint16_t>(current_offset - page_size)
                : 0;
        if (next_offset == current_offset)
        {
            conversation_->setHistoryPaging(chat_snapshot_buffer_.has_older_messages,
                                            false,
                                            current_offset,
                                            chat_snapshot_buffer_.message_total_count);
            ::ui::feedback::show_notice("Latest messages", 1400);
            return;
        }
        chat_model_.setMessageOffset(next_offset);
        if (!loadChatSnapshot() || chat_snapshot_buffer_.message_count == 0)
        {
            chat_model_.setMessageOffset(current_offset);
            (void)loadChatSnapshot();
            conversation_->setHistoryPaging(chat_snapshot_buffer_.has_older_messages,
                                            false,
                                            current_offset,
                                            chat_snapshot_buffer_.message_total_count);
            ::ui::feedback::show_notice("Latest messages", 1400);
            return;
        }
        applySnapshotMessagesToConversation(chat_snapshot_buffer_,
                                            *conversation_,
                                            ConversationScrollAnchor::Top);
        return;
    }

    if (intent == ChatConversationScreen::ActionIntent::Reply)
    {
        if (!team_conv_active_ && current_conv_.protocol != chat_support::active_mesh_protocol())
        {
            ::ui::feedback::show_notice("Reply disabled for this protocol", 2000);
            return;
        }
        if (!team_conv_active_ && !chat_support::supports_local_text_chat())
        {
            ::ui::feedback::show_notice(chat_support::local_text_chat_unavailable_message(), 2200);
            return;
        }
        if (team_conv_active_ && !chat_support::supports_team_chat())
        {
            ::ui::feedback::show_notice(chat_support::team_chat_unavailable_message(), 2200);
            return;
        }
        switchToCompose(current_conv_);
    }
}

void UiController::handleConversationMessageAction(
    ChatConversationScreen::MessageActionIntent intent,
    ::ui::chat::MessageRef ref)
{
    if (intent != ChatConversationScreen::MessageActionIntent::Retry)
    {
        return;
    }
    if (team_conv_active_ || delivery_action_adapter_ == nullptr)
    {
        ::ui::feedback::show_notice("Retry unavailable", 1800);
        return;
    }
    if (current_conv_.protocol != chat_support::active_mesh_protocol())
    {
        ::ui::feedback::show_notice("Retry disabled for this protocol", 2000);
        return;
    }

    const auto result = delivery_action_adapter_->retryMessage(ref);
    if (!result.ok)
    {
        ::ui::feedback::show_notice(
            delivery_retry_failure_message(result.failure),
            1800);
        return;
    }

    ::ui::feedback::show_notice("Retry queued", 1400);
    conversation_list_dirty_ = true;
    reloadConversationView();
}

void UiController::handleComposeAction(ChatComposeScreen::ActionIntent intent)
{
    if (!compose_)
    {
        return;
    }
    if (isTeamPositionPickerOpen())
    {
        if (intent == ChatComposeScreen::ActionIntent::Cancel)
        {
            onTeamPositionCancel();
        }
        return;
    }
    if (intent == ChatComposeScreen::ActionIntent::Cancel)
    {
        switchToConversation(current_conv_);
        return;
    }

    if (team_conv_active_)
    {
        if (intent == ChatComposeScreen::ActionIntent::Position)
        {
            openTeamPositionPicker();
            return;
        }

        std::string text = compose_->getText();
        if (text.empty())
        {
            switchToConversation(current_conv_);
            return;
        }
        const ::ui::UiActionResult result =
            team_workflow_.sendText(text.c_str());
        ::ui::feedback::show_notice(
            result.ok ? "Sent" : team_workflow_.textSendFailureMessage(result),
            result.ok ? 1400 : 2000);
        handleComposeSendDone(result.ok, false);
        return;
    }

    if (intent == ChatComposeScreen::ActionIntent::Send)
    {
        std::string text = compose_->getText();
        if (!text.empty())
        {
            handleSendMessage(text);
            return;
        }
    }
    switchToConversation(current_conv_);
}

void UiController::exitToMenu()
{
    if (exiting_)
    {
        return;
    }
    closeTeamPositionPicker(false);
    closeConversationInfoModal(false);
    exiting_ = true;
    stopTeamConversationTimer();
    team_conv_active_ = false;
    if (exit_request_)
    {
        exit_request_(exit_request_user_data_);
    }
    else
    {
        ui_request_exit_to_menu();
    }
}

} // namespace ui
} // namespace chat
