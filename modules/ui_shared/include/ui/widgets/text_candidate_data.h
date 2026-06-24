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

constexpr std::size_t kMaxBuiltinTextCandidates = 100;

const char* title(CandidateSet set);
const char* button_label(CandidateSet set);
std::size_t count(CandidateSet set);
const char* at(CandidateSet set, std::size_t index);

const std::uint8_t* emoji_core_binfont_data();
std::size_t emoji_core_binfont_size();
const std::uint8_t* symbol_core_binfont_data();
std::size_t symbol_core_binfont_size();

} // namespace ui::widgets::text_candidates
