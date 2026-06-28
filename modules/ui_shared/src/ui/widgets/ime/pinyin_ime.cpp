/**
 * @file pinyin_ime.cpp
 * @brief Lightweight pinyin IME engine (fixed dict, 8-char buffer, 50 candidates)
 */

#include "ui/widgets/ime/pinyin_ime.h"
#include "ui/widgets/ime/pinyin_lookup.h"

#include <algorithm>

namespace ui
{
namespace widgets
{

void PinyinIme::setEnabled(bool enabled)
{
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled_)
    {
        reset();
    }
}

bool PinyinIme::isEnabled() const
{
    return enabled_;
}

void PinyinIme::reset()
{
    buffer_.clear();
    candidates_.clear();
    candidate_index_ = 0;
}

bool PinyinIme::hasBuffer() const
{
    return !buffer_.empty();
}

const std::string& PinyinIme::buffer() const
{
    return buffer_;
}

const std::vector<std::string>& PinyinIme::candidates() const
{
    return candidates_;
}

int PinyinIme::candidateIndex() const
{
    return candidate_index_;
}

bool PinyinIme::appendLetter(char c)
{
    if (!enabled_) return false;
    if (buffer_.size() >= kMaxBufferLen) return false;
    if (!(c >= 'a' && c <= 'z')) return false;
    buffer_.push_back(c);
    updateCandidates();
    return true;
}

bool PinyinIme::backspace()
{
    if (!enabled_) return false;
    if (buffer_.empty()) return false;
    buffer_.pop_back();
    updateCandidates();
    return true;
}

bool PinyinIme::moveCandidate(int delta)
{
    if (!enabled_) return false;
    if (candidates_.empty()) return false;
    int size = static_cast<int>(candidates_.size());
    candidate_index_ = (candidate_index_ + delta) % size;
    if (candidate_index_ < 0)
    {
        candidate_index_ += size;
    }
    return true;
}

bool PinyinIme::commitCandidate(int index, std::string& out)
{
    if (!enabled_) return false;
    if (buffer_.empty()) return false;

    if (index >= 0 && index < static_cast<int>(candidates_.size()))
    {
        out = candidates_[index];
    }
    else if (!candidates_.empty())
    {
        out = candidates_[0];
    }
    else
    {
        out = buffer_;
    }
    reset();
    return true;
}

bool PinyinIme::commitActive(std::string& out)
{
    return commitCandidate(candidate_index_, out);
}

void PinyinIme::updateCandidates()
{
    candidates_.clear();
    candidate_index_ = 0;
    if (!enabled_) return;
    if (buffer_.empty()) return;
    updateCandidatesFromBuiltin();
}

void PinyinIme::updateCandidatesFromBuiltin()
{
    static constexpr size_t kMaxCandidates = 50;
    auto add_candidate = [this](const char* candidate) -> bool
    {
        if (!candidate || candidate[0] == '\0')
        {
            return candidates_.size() >= kMaxCandidates;
        }
        if (std::find(candidates_.begin(), candidates_.end(), candidate) == candidates_.end())
        {
            candidates_.emplace_back(candidate);
        }
        return candidates_.size() >= kMaxCandidates;
    };

    ime::collectPinyinCandidates(buffer_.c_str(), add_candidate);
}

} // namespace widgets
} // namespace ui
