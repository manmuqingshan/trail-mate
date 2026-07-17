/**
 * @file lxmf_lxst_telephony_client.cpp
 * @brief Sideband/LXST telephony runtime owner.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_lxst_telephony_client.h"

namespace chat::lxmf::runtime
{

uint8_t* LxstTelephonyClient::scratch()
{
    return scratch_;
}

const uint8_t* LxstTelephonyClient::scratch() const
{
    return scratch_;
}

std::size_t LxstTelephonyClient::scratchCapacity() const
{
    return sizeof(scratch_);
}

} // namespace chat::lxmf::runtime
