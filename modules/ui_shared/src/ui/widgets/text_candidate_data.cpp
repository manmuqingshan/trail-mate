#include "ui/widgets/text_candidate_data.h"

#include "text_candidate_builtin_emoji_data.h"
#include "text_candidate_builtin_symbol_data.h"

namespace ui::widgets::text_candidates
{
namespace
{

static constexpr const char* kSymbolCandidates[] = {
    "!",
    "\"",
    "#",
    "$",
    "%",
    "&",
    "'",
    "(",
    ")",
    "*",
    "+",
    ",",
    "-",
    ".",
    "/",
    ":",
    ";",
    "<",
    "=",
    ">",
    "?",
    "@",
    "[",
    "\\",
    "]",
    "^",
    "_",
    "`",
    "{",
    "|",
    "}",
    "~",
    "¡",
    "¿",
    "§",
    "¶",
    "©",
    "®",
    "™",
    "°",
    "±",
    "×",
    "÷",
    "µ",
    "π",
    "∞",
    "≈",
    "≠",
    "≤",
    "≥",
    "√",
    "∑",
    "∫",
    "∂",
    "∆",
    "∇",
    "∴",
    "∵",
    "←",
    "↑",
    "→",
    "↓",
    "↔",
    "↕",
    "↖",
    "↗",
    "↘",
    "↙",
    "⇐",
    "⇑",
    "⇒",
    "⇓",
    "⇔",
    "↩",
    "↪",
    "↻",
    "↺",
    "•",
    "◦",
    "·",
    "…",
    "—",
    "–",
    "†",
    "‡",
    "※",
    "№",
    "★",
    "☆",
    "♥",
    "♡",
    "◆",
    "◇",
    "●",
    "○",
    "■",
    "□",
    "▲",
    "△",
    "▼",
};

static_assert(sizeof(kSymbolCandidates) / sizeof(kSymbolCandidates[0]) <=
                  kMaxBuiltinSymbolCandidates,
              "symbol candidate list must stay within the built-in cap");
static_assert(text_candidate_data::kEmojiCandidateCount == kMaxBuiltinEmojiCandidates,
              "emoji candidate list must contain the approved built-in catalog");
static_assert(text_candidate_data::kEmojiCategoryCount == 9,
              "emoji catalog must expose the approved category count");

const char* const* candidates_for(CandidateSet set)
{
    return set == CandidateSet::Emoji
               ? text_candidate_data::kEmojiCandidates
               : kSymbolCandidates;
}

} // namespace

const char* title(CandidateSet set)
{
    return set == CandidateSet::Emoji ? "Emoji" : "Symbols";
}

const char* button_label(CandidateSet set)
{
    return set == CandidateSet::Emoji ? "Emoji" : "Sym";
}

std::size_t count(CandidateSet set)
{
    return set == CandidateSet::Emoji
               ? text_candidate_data::kEmojiCandidateCount
               : sizeof(kSymbolCandidates) / sizeof(kSymbolCandidates[0]);
}

const char* at(CandidateSet set, std::size_t index)
{
    if (index >= count(set))
    {
        return nullptr;
    }
    return candidates_for(set)[index];
}

std::size_t emoji_category_count()
{
    return text_candidate_data::kEmojiCategoryCount;
}

const EmojiCategoryInfo* emoji_category_at(std::size_t category_index)
{
    if (category_index >= emoji_category_count())
    {
        return nullptr;
    }
    return &text_candidate_data::kEmojiCategories[category_index];
}

const char* emoji_at(std::size_t category_index, std::size_t candidate_index)
{
    const EmojiCategoryInfo* category = emoji_category_at(category_index);
    if (category == nullptr || candidate_index >= category->count)
    {
        return nullptr;
    }
    return text_candidate_data::kEmojiCandidates[category->first + candidate_index];
}

const std::uint8_t* emoji_core_binfont_data()
{
    return text_candidate_data::kEmojiCoreBinfont;
}

std::size_t emoji_core_binfont_size()
{
    return text_candidate_data::kEmojiCoreBinfontSize;
}

const std::uint8_t* symbol_core_binfont_data()
{
    return text_candidate_data::kSymbolCoreBinfont;
}

std::size_t symbol_core_binfont_size()
{
    return text_candidate_data::kSymbolCoreBinfontSize;
}

} // namespace ui::widgets::text_candidates
