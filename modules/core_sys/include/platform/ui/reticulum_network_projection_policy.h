/**
 * @file reticulum_network_projection_policy.h
 * @brief Product projection policy for non-contact Reticulum announces.
 */

#pragma once

#include "platform/ui/reticulum_directory_runtime.h"

#include <cstdint>

namespace platform::ui::reticulum_network
{

enum class ProjectionBucket : uint8_t
{
    Hidden = 0,
    MessageRelay = 1,
    WebOrService = 2,
    TelephonyService = 3,
    UnknownService = 4,
};

inline ProjectionBucket classify(
    const reticulum_directory::AnnounceRecord& record)
{
    if (!record.valid)
    {
        return ProjectionBucket::Hidden;
    }

    switch (record.aspect)
    {
    case reticulum_directory::AnnounceAspect::LxmfPropagation:
        return ProjectionBucket::MessageRelay;
    case reticulum_directory::AnnounceAspect::NomadNetworkNode:
        return ProjectionBucket::WebOrService;
    case reticulum_directory::AnnounceAspect::CallAudio:
        return ProjectionBucket::TelephonyService;
    case reticulum_directory::AnnounceAspect::Unknown:
        return ProjectionBucket::UnknownService;
    case reticulum_directory::AnnounceAspect::LxmfDelivery:
    default:
        return ProjectionBucket::Hidden;
    }
}

inline bool visible(const reticulum_directory::AnnounceRecord& record)
{
    return classify(record) != ProjectionBucket::Hidden;
}

} // namespace platform::ui::reticulum_network
