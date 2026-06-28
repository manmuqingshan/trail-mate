#include "ui/widgets/ime/pinyin_lookup.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace
{
std::vector<std::string> collect(const char* preedit)
{
    std::vector<std::string> out;
    auto add_candidate = [&out](const char* candidate) -> bool
    {
        if (std::find(out.begin(), out.end(), candidate) == out.end())
        {
            out.emplace_back(candidate);
        }
        return out.size() >= 7;
    };
    ui::widgets::ime::collectPinyinCandidates(preedit, add_candidate);
    return out;
}

std::vector<std::string> collectDigits(const char* digits)
{
    std::vector<std::string> out;
    auto add_candidate = [&out](const char* candidate) -> bool
    {
        if (std::find(out.begin(), out.end(), candidate) == out.end())
        {
            out.emplace_back(candidate);
        }
        return out.size() >= 7;
    };
    ui::widgets::ime::collectPinyinSpellingsForDigits(digits, add_candidate);
    return out;
}

bool contains(const std::vector<std::string>& values, const char* expected)
{
    return std::find(values.begin(), values.end(), expected) != values.end();
}
} // namespace

int main()
{
    const std::vector<std::string> zhong = collect("zhong");
    assert(!zhong.empty());
    assert(zhong.front() == u8"中");

    const std::vector<std::string> zhong_upper = collect("ZHONG");
    assert(!zhong_upper.empty());
    assert(zhong_upper.front() == u8"中");

    const std::vector<std::string> nihao = collect("nihao");
    assert(contains(nihao, u8"你好"));

    const std::vector<std::string> shoudao = collect("shoudao");
    assert(contains(shoudao, u8"收到"));

    const std::vector<std::string> t9_zhong = collectDigits("94664");
    assert(contains(t9_zhong, "zhong"));

    return 0;
}
