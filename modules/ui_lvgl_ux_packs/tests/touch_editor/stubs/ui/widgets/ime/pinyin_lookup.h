#pragma once
#include <cstring>
namespace ui::widgets::ime
{
// Deterministic language-pack fixture; the composition engine and UI are real.
template <typename Add>
void collectPinyinCandidates(const char* input, Add add)
{
    if (std::strcmp(input, "ni") == 0)
    {
        add("你");
        add("泥");
    }
}
} // namespace ui::widgets::ime
