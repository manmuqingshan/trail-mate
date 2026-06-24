#include "linux_sim_runtime_renderer.h"

namespace trailmate::apps::linux_sim_shell
{

bool LinuxSimRuntimeRenderer::render(const LinuxSimRuntimeEntry& entry)
{
    ready_ = false;
    primary_ = false;

    if (entry.usingPrimaryScreenGraph())
    {
        ready_ = descriptor_renderer_.render(entry.adoption());
        primary_ = ready_;
        return ready_;
    }

    return false;
}

bool LinuxSimRuntimeRenderer::ready() const noexcept
{
    return ready_;
}

bool LinuxSimRuntimeRenderer::usingPrimaryScreenGraph() const noexcept
{
    return primary_;
}

bool LinuxSimRuntimeRenderer::usedPrimaryScreenGraph() const noexcept
{
    return primary_;
}

std::size_t LinuxSimRuntimeRenderer::lineCount() const noexcept
{
    return ready_ ? descriptor_renderer_.lineCount() : 0;
}

const trailmate::linux_sim::AsciiRenderLine*
LinuxSimRuntimeRenderer::lines() const noexcept
{
    return ready_ ? descriptor_renderer_.lines() : nullptr;
}

const char* LinuxSimRuntimeRenderer::line(std::size_t index) const noexcept
{
    return ready_ ? descriptor_renderer_.line(index) : nullptr;
}

} // namespace trailmate::apps::linux_sim_shell
