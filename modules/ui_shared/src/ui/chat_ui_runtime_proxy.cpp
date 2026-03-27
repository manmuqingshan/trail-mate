#include "ui/chat_ui_runtime_proxy.h"

#include "app/app_facade_access.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/contact_service.h"
#include "lvgl.h"
#include "sys/event_bus.h"
#include "ui/app_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/ui_common.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui/widgets/system_notification.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace chat::ui
{
namespace
{

std::string resolve_contact_name(chat::NodeId node_id)
{
    std::string name = app::messagingFacade().getContactService().getContactName(node_id);
    if (!name.empty())
    {
        return name;
    }

    char fallback[16] = {};
    std::snprintf(fallback, sizeof(fallback), "%08lX", static_cast<unsigned long>(node_id));
    return fallback;
}

lv_obj_t* resolve_overlay_parent()
{
    if (lv_obj_t* active_screen = lv_screen_active())
    {
        return active_screen;
    }
    return main_screen;
}
} // namespace

class GlobalChatUiRuntime::KeyVerificationModalRuntime
{
  public:
    ~KeyVerificationModalRuntime()
    {
        close(false);
    }

    void handleEvent(sys::Event* event)
    {
        if (!event)
        {
            return;
        }

        switch (event->type)
        {
        case sys::EventType::KeyVerificationNumberRequest:
        {
            auto* kv_event = static_cast<sys::KeyVerificationNumberRequestEvent*>(event);
            openNumber(kv_event->node_id, kv_event->nonce);
            return;
        }
        case sys::EventType::KeyVerificationNumberInform:
        {
            auto* kv_event = static_cast<sys::KeyVerificationNumberInformEvent*>(event);
            openInfo(kv_event->node_id, kv_event->security_number);
            return;
        }
        case sys::EventType::KeyVerificationFinal:
        {
            auto* kv_event = static_cast<sys::KeyVerificationFinalEvent*>(event);
            openFinal(kv_event->node_id, kv_event->verification_code, kv_event->is_sender);
            return;
        }
        default:
            return;
        }
    }

  private:
    static void submit_event_cb(lv_event_t* e)
    {
        auto* runtime = static_cast<KeyVerificationModalRuntime*>(lv_event_get_user_data(e));
        if (!runtime)
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
            runtime->submitNumber();
        }
    }

    static void close_event_cb(lv_event_t* e)
    {
        auto* runtime = static_cast<KeyVerificationModalRuntime*>(lv_event_get_user_data(e));
        if (!runtime)
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
            runtime->close(true);
        }
    }

    static void trust_event_cb(lv_event_t* e)
    {
        auto* runtime = static_cast<KeyVerificationModalRuntime*>(lv_event_get_user_data(e));
        if (!runtime)
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
            runtime->trustKey();
        }
    }

    void clearError()
    {
        if (error_label_)
        {
            lv_label_set_text(error_label_, "");
        }
    }

    void close(bool restore_group)
    {
        if (ime_)
        {
            ime_->detach();
            ime_.reset();
        }

        if (overlay_)
        {
            ui_set_overlay_active(false);
        }

        if (group_ && lv_group_get_default() == group_)
        {
            if (restore_group)
            {
                lv_group_t* restore_target = prev_group_ ? prev_group_ : app_g;
                set_default_group(restore_target);
            }
            else
            {
                set_default_group(nullptr);
            }
        }

        if (overlay_ && lv_obj_is_valid(overlay_))
        {
            lv_obj_del(overlay_);
        }
        if (group_)
        {
            lv_group_del(group_);
        }

        overlay_ = nullptr;
        panel_ = nullptr;
        desc_ = nullptr;
        textarea_ = nullptr;
        error_label_ = nullptr;
        group_ = nullptr;
        prev_group_ = nullptr;
        node_id_ = 0;
        nonce_ = 0;
        expects_number_ = false;
        can_trust_ = false;
    }

    bool beginModal(chat::NodeId node_id)
    {
        close(false);

        lv_obj_t* parent = resolve_overlay_parent();
        if (!parent)
        {
            return false;
        }

        node_id_ = node_id;
        prev_group_ = lv_group_get_default();
        group_ = lv_group_create();
        set_default_group(group_);

        overlay_ = lv_obj_create(parent);
        lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay_, LV_OPA_50, 0);
        lv_obj_set_style_border_width(overlay_, 0, 0);
        lv_obj_set_style_pad_all(overlay_, 0, 0);
        lv_obj_set_style_radius(overlay_, 0, 0);
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(overlay_);
        ui_set_overlay_active(true);
        return true;
    }

    lv_obj_t* createPanel(int width, int height, const char* title_text)
    {
        const auto modal_size = ::ui::page_profile::resolve_modal_size(width, height, overlay_);

        panel_ = lv_obj_create(overlay_);
        lv_obj_set_size(panel_, modal_size.width, modal_size.height);
        lv_obj_center(panel_);
        lv_obj_set_style_bg_color(panel_, lv_color_hex(0xFAF0D8), 0);
        lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(panel_, 1, 0);
        lv_obj_set_style_border_color(panel_, lv_color_hex(0xE7C98F), 0);
        lv_obj_set_style_radius(panel_, 10, 0);
        lv_obj_set_style_pad_all(panel_, ::ui::page_profile::resolve_modal_pad(), 0);
        lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* title = lv_label_create(panel_);
        lv_label_set_text(title, title_text);
        lv_obj_set_style_text_color(title, lv_color_hex(0x6B4A1E), 0);
        lv_obj_set_style_text_font(title, &lv_font_noto_cjk_16_2bpp, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
        return panel_;
    }

    void openNumber(chat::NodeId node_id, uint64_t nonce)
    {
        if (!beginModal(node_id))
        {
            return;
        }

        nonce_ = nonce;
        expects_number_ = true;
        can_trust_ = false;

        const auto& profile = ::ui::page_profile::current();
        const int modal_width = profile.large_touch_hitbox ? 560 : 320;
        const int modal_height = profile.large_touch_hitbox ? 380 : 220;
        createPanel(modal_width, modal_height, "Key Verification");

        std::string desc = "Enter number for ";
        desc += resolve_contact_name(node_id);
        desc_ = lv_label_create(panel_);
        lv_label_set_text(desc_, desc.c_str());
        lv_obj_set_width(desc_, LV_PCT(100));
        lv_obj_set_style_text_align(desc_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(desc_, lv_color_hex(0x8A6A3A), 0);
        lv_obj_align(desc_, LV_ALIGN_TOP_MID, 0, 34);

        textarea_ = lv_textarea_create(panel_);
        lv_obj_set_width(textarea_, LV_PCT(100));
        lv_textarea_set_one_line(textarea_, true);
        lv_textarea_set_placeholder_text(textarea_, "6 digits");
        lv_textarea_set_accepted_chars(textarea_, "0123456789");
        lv_textarea_set_max_length(textarea_, 6);
        lv_obj_align(textarea_, LV_ALIGN_TOP_MID, 0, 72);

        error_label_ = lv_label_create(panel_);
        lv_label_set_text(error_label_, "");
        lv_obj_set_width(error_label_, LV_PCT(100));
        lv_obj_set_style_text_align(error_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(error_label_, lv_color_hex(0xB94A2C), 0);
        lv_obj_align(error_label_, LV_ALIGN_TOP_MID, 0, 110);

        lv_obj_t* submit_btn = lv_btn_create(panel_);
        lv_obj_set_size(submit_btn, LV_PCT(48), ::ui::page_profile::resolve_control_button_height());
        lv_obj_align(submit_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_t* submit_label = lv_label_create(submit_btn);
        lv_label_set_text(submit_label, "Submit");
        lv_obj_center(submit_label);

        lv_obj_t* cancel_btn = lv_btn_create(panel_);
        lv_obj_set_size(cancel_btn, LV_PCT(48), ::ui::page_profile::resolve_control_button_height());
        lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_t* cancel_label = lv_label_create(cancel_btn);
        lv_label_set_text(cancel_label, "Cancel");
        lv_obj_center(cancel_label);

        lv_obj_add_event_cb(submit_btn, submit_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(submit_btn, submit_event_cb, LV_EVENT_KEY, this);
        lv_obj_add_event_cb(cancel_btn, close_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(cancel_btn, close_event_cb, LV_EVENT_KEY, this);

        lv_group_add_obj(group_, textarea_);
        lv_group_add_obj(group_, submit_btn);
        lv_group_add_obj(group_, cancel_btn);
        lv_group_focus_obj(textarea_);

        if (profile.large_touch_hitbox)
        {
            ime_.reset(new ::ui::widgets::ImeWidget());
            ime_->init(panel_, textarea_);
            ime_->setMode(::ui::widgets::ImeWidget::Mode::NUM);
        }
    }

    void openInfo(chat::NodeId node_id, uint32_t number)
    {
        if (!beginModal(node_id))
        {
            return;
        }

        expects_number_ = false;
        can_trust_ = false;

        createPanel(360, 220, "Verification Number");

        char number_buf[24] = {};
        std::snprintf(number_buf, sizeof(number_buf), "%03u %03u", number / 1000U, number % 1000U);

        desc_ = lv_label_create(panel_);
        std::string desc = resolve_contact_name(node_id) + "\nShare this number:\n";
        desc += number_buf;
        lv_label_set_text(desc_, desc.c_str());
        lv_obj_set_width(desc_, LV_PCT(100));
        lv_obj_set_style_text_align(desc_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(desc_, LV_ALIGN_CENTER, 0, -12);

        lv_obj_t* close_btn = lv_btn_create(panel_);
        lv_obj_set_size(close_btn, LV_PCT(100), ::ui::page_profile::resolve_control_button_height());
        lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_t* close_label = lv_label_create(close_btn);
        lv_label_set_text(close_label, "OK");
        lv_obj_center(close_label);
        lv_obj_add_event_cb(close_btn, close_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(close_btn, close_event_cb, LV_EVENT_KEY, this);
        lv_group_add_obj(group_, close_btn);
        lv_group_focus_obj(close_btn);
    }

    void openFinal(chat::NodeId node_id, const char* code, bool is_sender)
    {
        if (!beginModal(node_id))
        {
            return;
        }

        expects_number_ = false;
        can_trust_ = true;

        createPanel(420, 260, "Compare Verification Code");

        std::string desc = resolve_contact_name(node_id);
        desc += "\n";
        desc += is_sender ? "Send this code and compare:\n" : "Confirm received code:\n";
        desc += (code && code[0] != '\0') ? code : "--------";
        desc += "\n\nIf it matches, trust the key.";
        desc_ = lv_label_create(panel_);
        lv_label_set_text(desc_, desc.c_str());
        lv_obj_set_width(desc_, LV_PCT(100));
        lv_obj_set_style_text_align(desc_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(desc_, LV_ALIGN_CENTER, 0, -8);

        lv_obj_t* trust_btn = lv_btn_create(panel_);
        lv_obj_set_size(trust_btn, LV_PCT(48), ::ui::page_profile::resolve_control_button_height());
        lv_obj_align(trust_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_t* trust_label = lv_label_create(trust_btn);
        lv_label_set_text(trust_label, "Trust Key");
        lv_obj_center(trust_label);

        lv_obj_t* close_btn = lv_btn_create(panel_);
        lv_obj_set_size(close_btn, LV_PCT(48), ::ui::page_profile::resolve_control_button_height());
        lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_t* close_label = lv_label_create(close_btn);
        lv_label_set_text(close_label, "Later");
        lv_obj_center(close_label);

        lv_obj_add_event_cb(trust_btn, trust_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(trust_btn, trust_event_cb, LV_EVENT_KEY, this);
        lv_obj_add_event_cb(close_btn, close_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(close_btn, close_event_cb, LV_EVENT_KEY, this);
        lv_group_add_obj(group_, trust_btn);
        lv_group_add_obj(group_, close_btn);
        lv_group_focus_obj(trust_btn);
    }

    void submitNumber()
    {
        if (!expects_number_ || !textarea_)
        {
            return;
        }

        clearError();
        const char* text = lv_textarea_get_text(textarea_);
        if (!text || text[0] == '\0')
        {
            if (error_label_)
            {
                lv_label_set_text(error_label_, "Enter the 6-digit number");
            }
            return;
        }

        char* end_ptr = nullptr;
        unsigned long parsed = std::strtoul(text, &end_ptr, 10);
        if (!end_ptr || *end_ptr != '\0' || parsed > 999999UL)
        {
            if (error_label_)
            {
                lv_label_set_text(error_label_, "Invalid number");
            }
            return;
        }

        chat::IMeshAdapter* mesh = app::messagingFacade().getMeshAdapter();
        if (!mesh)
        {
            if (error_label_)
            {
                lv_label_set_text(error_label_, "Mesh unavailable");
            }
            return;
        }

        const bool ok = mesh->submitKeyVerificationNumber(node_id_, nonce_, static_cast<uint32_t>(parsed));
        if (!ok)
        {
            if (error_label_)
            {
                lv_label_set_text(error_label_, "Submit failed");
            }
            return;
        }

        ::ui::SystemNotification::show("Verification number sent", 2000);
        close(true);
    }

    void trustKey()
    {
        if (!can_trust_ || node_id_ == 0)
        {
            close(true);
            return;
        }

        bool ok = app::messagingFacade().getContactService().setNodeKeyManuallyVerified(node_id_, true);
        ::ui::SystemNotification::show(ok ? "Key marked trusted" : "Key trust failed", 2000);
        close(true);
    }

    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* desc_ = nullptr;
    lv_obj_t* textarea_ = nullptr;
    lv_obj_t* error_label_ = nullptr;
    lv_group_t* group_ = nullptr;
    lv_group_t* prev_group_ = nullptr;
    std::unique_ptr<::ui::widgets::ImeWidget> ime_;
    chat::NodeId node_id_ = 0;
    uint64_t nonce_ = 0;
    bool expects_number_ = false;
    bool can_trust_ = false;
};

GlobalChatUiRuntime::GlobalChatUiRuntime()
    : key_verification_runtime_(new KeyVerificationModalRuntime())
{
}

GlobalChatUiRuntime::~GlobalChatUiRuntime() = default;

void GlobalChatUiRuntime::setActiveRuntime(IChatUiRuntime* runtime)
{
    active_runtime_ = runtime;
}

IChatUiRuntime* GlobalChatUiRuntime::getActiveRuntime() const
{
    return active_runtime_;
}

void GlobalChatUiRuntime::update()
{
    if (active_runtime_)
    {
        active_runtime_->update();
    }
}

void GlobalChatUiRuntime::onChatEvent(sys::Event* event)
{
    if (!event)
    {
        return;
    }

    if (active_runtime_)
    {
        active_runtime_->onChatEvent(event);
        return;
    }

    key_verification_runtime_->handleEvent(event);
    delete event;
}

ChatUiState GlobalChatUiRuntime::getState() const
{
    return active_runtime_ ? active_runtime_->getState() : ChatUiState::ChannelList;
}

bool GlobalChatUiRuntime::isTeamConversationActive() const
{
    return active_runtime_ ? active_runtime_->isTeamConversationActive() : false;
}

} // namespace chat::ui
