#include "platform/esp/arduino_common/chat/infra/auto_reply_observer.h"

#include "app/app_facade_access.h"
#include "chat/usecase/auto_reply_policy.h"
#include "platform/ui/auto_reply_settings.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"

#include <cctype>
#include <string>

namespace chat::infra
{
namespace
{

bool has_reply_text(const std::string& text)
{
    if (text.empty() || text.size() > platform::ui::auto_reply::kTextMaxBytes)
    {
        return false;
    }

    for (const unsigned char character : text)
    {
        if (!std::isspace(character))
        {
            return true;
        }
    }
    return false;
}

class AutoReplyObserver final : public chat::ChatService::IncomingMessageObserver
{
  public:
    explicit AutoReplyObserver(chat::ChatService& service) : service_(service)
    {
        service_.addIncomingMessageObserver(this);
    }

    ~AutoReplyObserver() override
    {
        service_.removeIncomingMessageObserver(this);
    }

    void onIncomingMessage(const chat::ChatMessage& message,
                           const chat::RxMeta* /*rx_meta*/) override
    {
        const bool enabled = platform::ui::settings_store::get_bool(
            platform::ui::auto_reply::kSettingsNamespace,
            platform::ui::auto_reply::kEnabledKey,
            false);
        if (!enabled)
        {
            return;
        }

        std::string reply_text;
        const bool has_saved_text = platform::ui::settings_store::get_string(
            platform::ui::auto_reply::kSettingsNamespace,
            platform::ui::auto_reply::kTextKey,
            reply_text);
        const chat::ConversationId conversation = chat::conversationIdForMessage(message);
        const chat::NodeId self_node = app::appFacade().getSelfNodeId();
        const chat::auto_reply::Context context{
            enabled,
            has_saved_text && has_reply_text(reply_text),
            platform::ui::screen::is_sleeping(),
            self_node != 0 && message.from == self_node,
            service_.canSendToConversation(conversation),
        };
        if (chat::auto_reply::decide(message, context) !=
            chat::auto_reply::Decision::Reply)
        {
            return;
        }

        (void)service_.sendTextToConversationDetailed(conversation, reply_text);
    }

  private:
    chat::ChatService& service_;
};

} // namespace

std::unique_ptr<chat::ChatService::IncomingMessageObserver>
create_auto_reply_observer(chat::ChatService& service)
{
    return std::unique_ptr<chat::ChatService::IncomingMessageObserver>(
        new AutoReplyObserver(service));
}

} // namespace chat::infra
