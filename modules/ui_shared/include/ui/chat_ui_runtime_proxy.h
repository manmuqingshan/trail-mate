#pragma once

#include "ui/chat_ui_runtime.h"

#include <memory>

namespace chat::ui
{

class GlobalChatUiRuntime final : public IChatUiRuntime
{
  public:
    GlobalChatUiRuntime();
    ~GlobalChatUiRuntime() override;

    void setActiveRuntime(IChatUiRuntime* runtime);
    IChatUiRuntime* getActiveRuntime() const;

    void update() override;
    void onChatEvent(sys::Event* event) override;
    ChatUiState getState() const override;
    bool isTeamConversationActive() const override;

  private:
    class KeyVerificationModalRuntime;

    IChatUiRuntime* active_runtime_ = nullptr;
    std::unique_ptr<KeyVerificationModalRuntime> key_verification_runtime_;
};

} // namespace chat::ui
