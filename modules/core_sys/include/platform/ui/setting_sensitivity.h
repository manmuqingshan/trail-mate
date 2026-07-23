#pragma once

#include <cctype>
#include <cstring>

namespace platform::ui::settings
{

enum class Sensitivity
{
    Public = 0,
    Secret,
};

inline bool contains_case_insensitive(const char* text, const char* needle)
{
    if (!text || !needle || needle[0] == '\0')
    {
        return false;
    }
    const std::size_t text_len = std::strlen(text);
    const std::size_t needle_len = std::strlen(needle);
    if (needle_len > text_len)
    {
        return false;
    }
    for (std::size_t i = 0; i + needle_len <= text_len; ++i)
    {
        std::size_t j = 0;
        for (; j < needle_len; ++j)
        {
            const auto lhs = static_cast<unsigned char>(text[i + j]);
            const auto rhs = static_cast<unsigned char>(needle[j]);
            if (std::tolower(lhs) != std::tolower(rhs))
            {
                break;
            }
        }
        if (j == needle_len)
        {
            return true;
        }
    }
    return false;
}

inline Sensitivity sensitivity_for_key(const char* key)
{
    static constexpr const char* kSecretMarkers[] = {
        "password",
        "passwd",
        "pass",
        "psk",
        "private",
        "secret",
        "token",
        "credential",
        "_key",
    };
    for (const char* marker : kSecretMarkers)
    {
        if (contains_case_insensitive(key, marker))
        {
            return Sensitivity::Secret;
        }
    }
    return Sensitivity::Public;
}

inline const char* diagnostic_value(const char* key, const char* value)
{
    return sensitivity_for_key(key) == Sensitivity::Secret
               ? "<redacted>"
               : (value ? value : "<null>");
}

} // namespace platform::ui::settings
