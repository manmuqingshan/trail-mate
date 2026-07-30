#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::widgets::text_candidates
{

enum class CandidateSet
{
    Symbols,
    Emoji,
};

constexpr std::size_t kMaxBuiltinSymbolCandidates = 100;
constexpr std::size_t kMaxBuiltinEmojiCandidates = 324;

struct EmojiCategoryInfo
{
    const char* id = nullptr;
    const char* title = nullptr;
    const char* icon = nullptr;
    std::size_t first = 0;
    std::size_t count = 0;
};

const char* title(CandidateSet set);
const char* button_label(CandidateSet set);
std::size_t count(CandidateSet set);
const char* at(CandidateSet set, std::size_t index);

std::size_t emoji_category_count();
const EmojiCategoryInfo* emoji_category_at(std::size_t category_index);
const char* emoji_at(std::size_t category_index, std::size_t candidate_index);

const std::uint8_t* emoji_core_binfont_data();
std::size_t emoji_core_binfont_size();
const std::uint8_t* symbol_core_binfont_data();
std::size_t symbol_core_binfont_size();

} // namespace ui::widgets::text_candidates
