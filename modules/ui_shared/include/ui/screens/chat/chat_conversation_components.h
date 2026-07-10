/**
 * @file chat_conversation_components.h
 * @brief Chat conversation screen
 */
#pragma once

#if defined(ARDUINO_T_WATCH_S3)
#include "../chat_watch/chat_conversation_components_watch.h"
#else

#include "chat/domain/chat_types.h"
#include "chat_conversation_input.h"
#include "lvgl.h"
#include "ui/widgets/map/map_viewport.h"
#include "ui/widgets/top_bar.h"
#include "ui_presentation/chat/chat_message_ref.h"
#include "ui_presentation/chat/chat_workspace_snapshot.h"
#include "ui_presentation/map/map_overlay_snapshot.h"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ui::chat
{
struct MessageRow;
}

namespace chat
{
namespace ui
{

class ChatConversationScreen
{
  public:
    enum class ActionIntent
    {
        Reply,
        LoadOlder,
        LoadNewer
    };

    enum class MessageActionIntent
    {
        Retry
    };

    ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv);
    ~ChatConversationScreen();

    void addMessage(const ::ui::chat::MessageRow& row);
    void clearMessages();
    void scrollToTop();
    void scrollToBottom();
    bool updateMessageStatus(chat::MessageId msg_id, chat::MessageStatus status);

    void setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data);
    void setMessageActionCallback(
        void (*cb)(MessageActionIntent intent,
                   ::ui::chat::MessageRef ref,
                   void*),
        void* user_data);
    bool isAlive() const { return guard_ && guard_->alive; }

    lv_obj_t* getObj() const { return container_; }
    lv_obj_t* getMsgList() const { return msg_list_; }
    lv_obj_t* getReplyBtn() const { return reply_btn_; }
    lv_obj_t* getBackBtn() const { return top_bar_.back_btn; }

    chat::ChannelId getChannel() const { return conv_.channel; }

    void setHeaderText(const char* title, const char* status = nullptr);
    void updateBatteryFromBoard();
    void setBackCallback(void (*cb)(void*), void* user_data);
    void setReplyEnabled(bool enabled);
    bool isReplyEnabled() const { return reply_enabled_; }
    void setHistoryPaging(bool has_older,
                          bool has_newer,
                          uint16_t offset,
                          uint16_t total_count);
    void setLocationOverlay(const ::ui::map::MapOverlaySnapshot& overlay);
    void toggleLocationMap();
    void cycleLocationMapLayer();
    bool isLocationMapVisible() const { return location_map_visible_; }

  private:
    enum class TimerDomain
    {
        ScreenGeneral,
        Input
    };

    struct TimerEntry
    {
        lv_timer_t* timer = nullptr;
        TimerDomain domain = TimerDomain::ScreenGeneral;
    };

    struct LifetimeGuard
    {
        bool alive = false;
        int pending_async = 0;
    };

    struct ActionContext
    {
        ChatConversationScreen* screen = nullptr;
        ActionIntent intent = ActionIntent::Reply;
    };

    struct ActionPayload
    {
        LifetimeGuard* guard = nullptr;
        void (*action_cb)(ActionIntent intent, void*) = nullptr;
        void* user_data = nullptr;
        ActionIntent intent = ActionIntent::Reply;
    };

    struct BackPayload
    {
        LifetimeGuard* guard = nullptr;
        void (*back_cb)(void*) = nullptr;
        void* user_data = nullptr;
    };

    struct MessageActionContext
    {
        ChatConversationScreen* screen = nullptr;
        MessageActionIntent intent = MessageActionIntent::Retry;
        ::ui::chat::MessageRef ref;
    };

    struct MessageActionPayload
    {
        LifetimeGuard* guard = nullptr;
        void (*message_action_cb)(MessageActionIntent intent,
                                  ::ui::chat::MessageRef ref,
                                  void*) = nullptr;
        void* user_data = nullptr;
        MessageActionIntent intent = MessageActionIntent::Retry;
        ::ui::chat::MessageRef ref;
    };

    lv_obj_t* container_ = nullptr;
    ::ui::widgets::TopBar top_bar_{};
    lv_obj_t* body_row_ = nullptr;
    lv_obj_t* right_column_ = nullptr;
    lv_obj_t* msg_list_ = nullptr;
    lv_obj_t* action_bar_ = nullptr;
    lv_obj_t* reply_btn_ = nullptr;
    lv_obj_t* compose_btn_ = nullptr; // kept for compatibility (not created in v0)
    lv_obj_t* location_panel_ = nullptr;
    lv_obj_t* location_map_host_ = nullptr;
    ::ui::widgets::map::Runtime location_map_runtime_{};
    ::ui::map::MapOverlaySnapshot location_overlay_{};
    chat::ConversationId conv_{};

    void (*action_cb_)(ActionIntent intent, void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    void (*back_cb_)(void*) = nullptr;
    void* back_cb_user_data_ = nullptr;
    void (*message_action_cb_)(MessageActionIntent intent,
                               ::ui::chat::MessageRef ref,
                               void*) = nullptr;
    void* message_action_cb_user_data_ = nullptr;

    struct MessageItem
    {
        ::ui::chat::MessageRef ref;
        ::ui::chat::MessageDeliveryState delivery =
            ::ui::chat::MessageDeliveryState::Unknown;
        lv_obj_t* container = nullptr; // row
        lv_obj_t* bubble = nullptr;
        lv_obj_t* meta_row = nullptr;
        lv_obj_t* sender_label = nullptr;
        lv_obj_t* source_label = nullptr;
        lv_obj_t* text_label = nullptr;   // inside bubble
        lv_obj_t* time_label = nullptr;   // inside meta row
        lv_obj_t* status_label = nullptr; // reserved (not used)
        std::unique_ptr<MessageActionContext> retry_ctx;
        bool retry_enabled = false;
    };

    std::vector<MessageItem> messages_;
    static constexpr size_t MAX_DISPLAY_MESSAGES =
        ::ui::chat::ChatWorkspaceSnapshot::kMaxMessages;

    LifetimeGuard* guard_ = nullptr;
    std::vector<TimerEntry> timers_;
    conversation::input::Binding input_binding_{};
    ActionContext reply_ctx_{};
    bool reply_enabled_ = true;
    bool history_has_older_ = false;
    bool history_has_newer_ = false;
    uint16_t history_offset_ = 0;
    uint16_t history_total_count_ = 0;
    bool history_auto_load_pending_ = false;
    bool history_scroll_position_valid_ = false;
    lv_coord_t history_last_scroll_y_ = 0;
    bool location_map_visible_ = false;
    bool location_map_created_ = false;

    void createMessageItem(const ::ui::chat::MessageRow& row);
    void handleScroll();
    void enableRetryAction(MessageItem& item);
    void disableRetryAction(MessageItem& item);
    void createLocationPanel();
    void ensureLocationMapCreated();
    void refreshLocationMap();
    void syncLocationMapVisibility();
    bool usesFloatingLocationMap() const;

    static void action_event_cb(lv_event_t* e);
    static void message_action_event_cb(lv_event_t* e);
    static void scroll_event_cb(lv_event_t* e);
    static void async_action_cb(void* user_data);
    static void async_message_action_cb(void* user_data);
    static void async_back_cb(void* user_data);
    static void on_root_deleted(lv_event_t* e);
    static void handle_back(void* user_data);

    lv_timer_t* add_timer(lv_timer_cb_t cb, uint32_t period_ms, void* user_data, TimerDomain domain);
    void clear_timers(TimerDomain domain);
    void clear_all_timers();
    void handle_root_deleted();

    void schedule_action_async(ActionIntent intent);
    void schedule_message_action_async(MessageActionIntent intent,
                                       ::ui::chat::MessageRef ref);
    void schedule_back_async();
};

} // namespace ui
} // namespace chat

#endif
