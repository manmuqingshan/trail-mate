#pragma once

#include "ui/widgets/ime/pinyin_data.h"

#include <cstddef>

namespace ui::widgets::ime
{
namespace detail
{
constexpr size_t kPinyinLookupPinyinMaxBytes = 16;
constexpr size_t kPinyinLookupCandidateMaxBytes = 64;

inline char readDictChar(const char*& cursor)
{
    const char value = static_cast<char>(pgm_read_byte(cursor));
    ++cursor;
    return value;
}

inline char lowerAscii(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

inline bool equalsIgnoreCase(const char* lhs, const char* rhs)
{
    if (!lhs || !rhs)
    {
        return false;
    }
    while (*lhs != '\0' && *rhs != '\0')
    {
        if (lowerAscii(*lhs) != lowerAscii(*rhs))
        {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

inline bool startsWithIgnoreCase(const char* text, const char* prefix)
{
    if (!text || !prefix)
    {
        return false;
    }
    while (*prefix != '\0')
    {
        if (*text == '\0' || lowerAscii(*text) != lowerAscii(*prefix))
        {
            return false;
        }
        ++text;
        ++prefix;
    }
    return true;
}

inline bool pinyinMatches(const char* pinyin, const char* preedit, bool exact)
{
    if (exact)
    {
        return equalsIgnoreCase(pinyin, preedit);
    }
    return startsWithIgnoreCase(pinyin, preedit) && !equalsIgnoreCase(pinyin, preedit);
}

template <typename Sink>
bool emitCandidate(char* candidate, size_t& candidate_len, Sink& sink)
{
    if (candidate_len == 0)
    {
        return false;
    }
    candidate[candidate_len] = '\0';
    candidate_len = 0;
    return sink(candidate);
}

template <typename Sink>
bool scanPinyinCandidates(const char* preedit, bool exact, Sink& sink)
{
    const char* cursor = ::kPinyinDict;
    while (true)
    {
        char c = readDictChar(cursor);
        if (c == '\0')
        {
            return false;
        }
        while (c == '\r' || c == '\n')
        {
            c = readDictChar(cursor);
            if (c == '\0')
            {
                return false;
            }
        }
        while (c == ' ' || c == '\t')
        {
            c = readDictChar(cursor);
            if (c == '\0')
            {
                return false;
            }
        }

        const bool comment = c == '#';
        char pinyin[kPinyinLookupPinyinMaxBytes] = {};
        size_t pinyin_len = 0;
        while (c != '\0' && c != '\r' && c != '\n' && c != ' ' && c != '\t')
        {
            if (pinyin_len + 1U < sizeof(pinyin))
            {
                pinyin[pinyin_len++] = c;
            }
            c = readDictChar(cursor);
        }
        pinyin[pinyin_len] = '\0';

        while (c == ' ' || c == '\t')
        {
            c = readDictChar(cursor);
        }

        const bool match = !comment && pinyinMatches(pinyin, preedit, exact);
        char candidate[kPinyinLookupCandidateMaxBytes] = {};
        size_t candidate_len = 0;
        while (c != '\0' && c != '\r' && c != '\n')
        {
            if (match)
            {
                if (c == ' ' || c == '\t')
                {
                    if (emitCandidate(candidate, candidate_len, sink))
                    {
                        return true;
                    }
                }
                else if (candidate_len + 1U < sizeof(candidate))
                {
                    candidate[candidate_len++] = c;
                }
            }
            c = readDictChar(cursor);
        }
        if (match && emitCandidate(candidate, candidate_len, sink))
        {
            return true;
        }
        if (c == '\0')
        {
            return false;
        }
    }
}
} // namespace detail

template <typename Sink>
void collectPinyinCandidates(const char* preedit, Sink&& sink)
{
    if (!preedit || preedit[0] == '\0')
    {
        return;
    }
    if (detail::scanPinyinCandidates(preedit, true, sink))
    {
        return;
    }
    (void)detail::scanPinyinCandidates(preedit, false, sink);
}
} // namespace ui::widgets::ime
