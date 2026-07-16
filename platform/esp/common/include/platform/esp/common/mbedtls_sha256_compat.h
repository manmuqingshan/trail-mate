#pragma once

#include "mbedtls/sha256.h"
#include "mbedtls/version.h"

#include <cstddef>
#include <cstdint>

namespace platform::esp::common::crypto
{

inline int sha256_starts(mbedtls_sha256_context* context, int is224)
{
#if MBEDTLS_VERSION_NUMBER < 0x03000000
    return mbedtls_sha256_starts_ret(context, is224);
#else
    return mbedtls_sha256_starts(context, is224);
#endif
}

inline int sha256_update(mbedtls_sha256_context* context,
                         const uint8_t* data,
                         std::size_t len)
{
#if MBEDTLS_VERSION_NUMBER < 0x03000000
    return mbedtls_sha256_update_ret(context, data, len);
#else
    return mbedtls_sha256_update(context, data, len);
#endif
}

inline int sha256_finish(mbedtls_sha256_context* context, uint8_t output[32])
{
#if MBEDTLS_VERSION_NUMBER < 0x03000000
    return mbedtls_sha256_finish_ret(context, output);
#else
    return mbedtls_sha256_finish(context, output);
#endif
}

} // namespace platform::esp::common::crypto
