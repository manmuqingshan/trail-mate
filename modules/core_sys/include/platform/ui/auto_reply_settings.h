#pragma once

#include <cstddef>

namespace platform::ui::auto_reply
{

inline constexpr char kSettingsNamespace[] = "settings";
inline constexpr char kEnabledKey[] = "chat_auto_reply_enabled";
inline constexpr char kTextKey[] = "chat_auto_reply_text";
inline constexpr std::size_t kTextMaxBytes = 171;

} // namespace platform::ui::auto_reply
