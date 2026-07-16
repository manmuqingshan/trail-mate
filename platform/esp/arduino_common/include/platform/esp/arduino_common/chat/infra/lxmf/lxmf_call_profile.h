/**
 * @file lxmf_call_profile.h
 * @brief Single embedded LXST media-profile mapping for the Reticulum client
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/reticulum/audio_call_wire.h"
#include "chat/infra/reticulum/lxst_telephony_wire.h"
#include "platform/ui/reticulum_call_runtime.h"

#include <cstdint>

namespace chat::lxmf::call_profile
{

inline constexpr uint16_t kEmbeddedLxstProfile =
    reticulum::lxst::kProfileBandwidthLow;

inline reticulum::audio_call::Codec2Mode audioCodec2Mode(
    uint16_t lxst_profile)
{
    (void)lxst_profile;
    return reticulum::audio_call::Codec2Mode::Mode3200;
}

inline ::platform::ui::reticulum_call::Codec2Mode runtimeCodec2Mode(
    ReticulumCallWireProfile wire_profile,
    uint16_t lxst_profile)
{
    if (wire_profile == ReticulumCallWireProfile::MeshChatCallAudio)
    {
        return ::platform::ui::reticulum_call::Codec2Mode::Mode1200;
    }

    (void)lxst_profile;
    return ::platform::ui::reticulum_call::Codec2Mode::Mode3200;
}

inline ::platform::ui::reticulum_call::WireProfile runtimeWireProfile(
    ReticulumCallWireProfile wire_profile)
{
    return wire_profile == ReticulumCallWireProfile::MeshChatCallAudio
               ? ::platform::ui::reticulum_call::WireProfile::MeshChatCallAudio
               : ::platform::ui::reticulum_call::WireProfile::SidebandLxst;
}

} // namespace chat::lxmf::call_profile
