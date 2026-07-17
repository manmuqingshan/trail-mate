/**
 * @file lxmf_lxst_telephony_client.h
 * @brief Sideband/LXST telephony runtime owner.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"

#include <cstddef>

namespace chat::lxmf::runtime
{

class LxstTelephonyClient
{
  public:
    LxstTelephonyClient() = default;
    LxstTelephonyClient(const LxstTelephonyClient&) = delete;
    LxstTelephonyClient& operator=(const LxstTelephonyClient&) = delete;
    LxstTelephonyClient(LxstTelephonyClient&&) = delete;
    LxstTelephonyClient& operator=(LxstTelephonyClient&&) = delete;

    uint8_t* scratch();
    const uint8_t* scratch() const;
    std::size_t scratchCapacity() const;

  private:
    uint8_t scratch_[reticulum::kReticulumMtu] = {};
};

} // namespace chat::lxmf::runtime
