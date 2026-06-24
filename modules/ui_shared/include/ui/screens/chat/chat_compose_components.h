#pragma once

#if defined(ARDUINO_T_WATCH_S3)
#include "../chat_watch/chat_compose_components_watch.h"
#else

#include "chat/domain/chat_types.h"
#include "lvgl.h"
#include "ui/widgets/top_bar.h"
#include <string>

namespace ui
{
namespace widgets
{
class ImeWidget;
} // namespace widgets
} // namespace ui

namespace chat::ui
{

class ChatComposeScreen
{
  public:
    enum class ActionIntent
    {
        Send,
        Position,
        Cancel
    };

    ChatComposeScreen(lv_obj_t* parent, chat::ConversationId conv);
    ~ChatComposeScreen();

    void setHeaderText(const char* title, const char* status = nullptr);
    void setActionLabels(const char* send_label, const char* cancel_label);
    void setPositionButton(const char* label, bool visible);
    std::string getText() const;
    void clearText();

    void setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data);
    void setBackCallback(void (*cb)(void*), void* user_data);

    void attachImeWidget(::ui::widgets::ImeWidget* widget);
    lv_obj_t* getTextarea() const;
    lv_obj_t* getContent() const;
    lv_obj_t* getActionBar() const;

    lv_obj_t* getObj() const;

  private:
    chat::ConversationId conv_;

    void (*action_cb_)(ActionIntent intent, void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    void (*back_cb_)(void*) = nullptr;
    void* back_cb_user_data_ = nullptr;

    struct Impl;
    struct LifetimeGuard;
    struct ActionPayload
    {
        LifetimeGuard* guard = nullptr;
        void (*action_cb)(ActionIntent intent, void*) = nullptr;
        void* user_data = nullptr;
        ActionIntent intent = ActionIntent::Send;
    };
    struct BackPayload
    {
        LifetimeGuard* guard = nullptr;
        void (*back_cb)(void*) = nullptr;
        void* user_data = nullptr;
    };
    Impl* impl_ = nullptr;

    void init_topbar();
    void refresh_len();
    void schedule_action_async(ActionIntent intent);
    void schedule_back_async();
    static void release_async_guard(LifetimeGuard* guard);
    static void async_action_cb(void* user_data);
    static void async_back_cb(void* user_data);
    static void on_root_deleted(lv_event_t* e);
    static void on_action_click(lv_event_t* e);
    static void on_text_changed(lv_event_t* e);
    static void on_key(lv_event_t* e);
    static void on_back(void* user_data);
    ::ui::widgets::ImeWidget* ime_widget_ = nullptr;
};

} // namespace chat::ui

#endif
