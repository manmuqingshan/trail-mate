#include "ui/widgets/foreground_operation_overlay.h"

#include "sys/clock.h"
#include "ui/widgets/progress_overlay_presenter.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace ui::widgets::foreground_operation
{
namespace
{

constexpr std::size_t kSlotCount = static_cast<std::size_t>(Slot::Count);

std::array<Snapshot, kSlotCount> s_slots{};
::ui::widgets::ProgressOverlayPresenter s_presenter{};
bool s_overlay_active = false;
Policy s_overlay_policy = Policy::Hidden;
Slot s_overlay_slot = Slot::Count;
std::uint32_t s_overlay_generation = 0;

bool valid_slot(Slot slot)
{
    return static_cast<std::size_t>(slot) < kSlotCount;
}

std::size_t slot_index(Slot slot)
{
    return static_cast<std::size_t>(slot);
}

bool overlay_policy(Policy policy)
{
    return policy == Policy::Overlay;
}

int priority_rank(Priority priority)
{
    return static_cast<int>(priority);
}

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!text || text[0] == '\0')
    {
        return;
    }
    std::strncpy(out, text, out_len - 1U);
    out[out_len - 1U] = '\0';
}

const Snapshot* select_overlay_snapshot()
{
    const Snapshot* best = nullptr;
    for (const Snapshot& candidate : s_slots)
    {
        if (!candidate.active || !overlay_policy(candidate.policy))
        {
            continue;
        }
        if (!best)
        {
            best = &candidate;
            continue;
        }
        const int candidate_priority = priority_rank(candidate.priority);
        const int best_priority = priority_rank(best->priority);
        if (candidate_priority > best_priority ||
            (candidate_priority == best_priority &&
             candidate.updated_ms >= best->updated_ms))
        {
            best = &candidate;
        }
    }
    return best;
}

bool same_overlay_selection(const Snapshot& snapshot)
{
    return s_overlay_active &&
           s_overlay_slot == snapshot.slot &&
           s_overlay_generation == snapshot.generation &&
           s_overlay_policy == snapshot.policy;
}

} // namespace

Snapshot make_snapshot(Slot slot,
                       Policy policy,
                       Priority priority,
                       const char* title,
                       const char* detail,
                       int progress_percent,
                       const char* result,
                       std::uint32_t generation)
{
    Snapshot snapshot{};
    snapshot.active = true;
    snapshot.slot = slot;
    snapshot.policy = policy;
    snapshot.priority = priority;
    snapshot.progress_percent = progress_percent;
    snapshot.generation = generation;
    snapshot.updated_ms = sys::millis_now();
    copy_text(snapshot.title, sizeof(snapshot.title), title);
    copy_text(snapshot.detail, sizeof(snapshot.detail), detail);
    copy_text(snapshot.result, sizeof(snapshot.result), result);
    return snapshot;
}

void publish(const Snapshot& snapshot)
{
    if (!valid_slot(snapshot.slot))
    {
        return;
    }

    Snapshot stored = snapshot;
    stored.slot = snapshot.slot;
    stored.updated_ms = sys::millis_now();
    if (stored.title[0] == '\0')
    {
        copy_text(stored.title, sizeof(stored.title), "Working...");
    }
    s_slots[slot_index(stored.slot)] = stored;
    sync_overlay();
}

void clear(Slot slot, std::uint32_t generation)
{
    if (!valid_slot(slot))
    {
        return;
    }

    Snapshot& current = s_slots[slot_index(slot)];
    if (generation != 0 && current.generation != generation)
    {
        return;
    }
    current = Snapshot{};
    current.slot = slot;
    current.updated_ms = sys::millis_now();
    sync_overlay();
}

void sync_overlay()
{
    const Snapshot* selected = select_overlay_snapshot();
    if (!selected)
    {
        if (!s_overlay_active)
        {
            return;
        }
        s_presenter.hide();
        s_overlay_active = false;
        s_overlay_policy = Policy::Hidden;
        s_overlay_slot = Slot::Count;
        s_overlay_generation = 0;
        ::ui::widgets::ProgressOverlayPresenter::request_present();
        return;
    }

    const bool same_selection = same_overlay_selection(*selected);
    s_presenter.show_or_update(selected->title,
                               selected->detail[0] != '\0' ? selected->detail : nullptr,
                               selected->progress_percent);
    s_overlay_active = true;
    s_overlay_policy = selected->policy;
    s_overlay_slot = selected->slot;
    s_overlay_generation = selected->generation;

    if (!same_selection)
    {
        ::ui::widgets::ProgressOverlayPresenter::request_present();
    }
}

bool slot_active(Slot slot)
{
    return valid_slot(slot) && s_slots[slot_index(slot)].active;
}

void reset_all()
{
    for (Snapshot& snapshot : s_slots)
    {
        snapshot = Snapshot{};
    }
    s_presenter.hide();
    s_overlay_active = false;
    s_overlay_policy = Policy::Hidden;
    s_overlay_slot = Slot::Count;
    s_overlay_generation = 0;
}

} // namespace ui::widgets::foreground_operation
