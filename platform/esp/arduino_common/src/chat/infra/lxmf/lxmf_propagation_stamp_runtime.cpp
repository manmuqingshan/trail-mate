/**
 * @file lxmf_propagation_stamp_runtime.cpp
 * @brief Incremental official LXMF propagation stamp generation for ESP
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_stamp_runtime.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"

#include <cstring>

#include <esp_heap_caps.h>
#include <esp_random.h>

namespace chat::lxmf::runtime
{
namespace
{

std::size_t packRoundNumber(uint16_t value, uint8_t out[3])
{
    if (value <= 0x7FU)
    {
        out[0] = static_cast<uint8_t>(value);
        return 1;
    }
    if (value <= 0xFFU)
    {
        out[0] = 0xCC;
        out[1] = static_cast<uint8_t>(value);
        return 2;
    }
    out[0] = 0xCD;
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    out[2] = static_cast<uint8_t>(value & 0xFFU);
    return 3;
}

} // namespace

PropagationStampRuntime::PropagationStampRuntime()
{
    mbedtls_sha256_init(&base_hash_);
}

PropagationStampRuntime::~PropagationStampRuntime()
{
    reset();
    mbedtls_sha256_free(&base_hash_);
}

bool PropagationStampRuntime::begin(
    const uint8_t transient_id[reticulum::kFullHashSize],
    uint8_t target_cost)
{
    if (!transient_id)
    {
        return false;
    }

    reset();
    workblock_ = static_cast<uint8_t*>(
        heap_caps_malloc_prefer(kWorkblockBytes,
                                2,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!workblock_)
    {
        state_ = State::Failed;
        return false;
    }

    std::memcpy(transient_id_, transient_id, sizeof(transient_id_));
    target_cost_ = target_cost;
    state_ = State::Expanding;
    return true;
}

PropagationStampRuntime::State PropagationStampRuntime::poll(
    uint16_t expand_round_budget,
    uint16_t search_round_budget)
{
    while (state_ == State::Expanding && expand_round_budget-- != 0)
    {
        if (!expandOneRound())
        {
            state_ = State::Failed;
            break;
        }
        if (expanded_rounds_ == kExpandRounds && !prepareSearch())
        {
            state_ = State::Failed;
            break;
        }
    }

    while (state_ == State::Searching && search_round_budget-- != 0)
    {
        if (!searchOneRound())
        {
            state_ = State::Failed;
            break;
        }
    }
    return state_;
}

bool PropagationStampRuntime::takeStamp(
    uint8_t out_stamp[reticulum::kFullHashSize])
{
    if (!out_stamp || state_ != State::Complete)
    {
        return false;
    }
    std::memcpy(out_stamp, stamp_, sizeof(stamp_));
    reset();
    return true;
}

void PropagationStampRuntime::reset()
{
    if (workblock_)
    {
        heap_caps_free(workblock_);
        workblock_ = nullptr;
    }
    if (base_hash_ready_)
    {
        mbedtls_sha256_free(&base_hash_);
        mbedtls_sha256_init(&base_hash_);
    }
    std::memset(transient_id_, 0, sizeof(transient_id_));
    std::memset(stamp_, 0, sizeof(stamp_));
    state_ = State::Idle;
    expanded_rounds_ = 0;
    search_rounds_ = 0;
    target_cost_ = 0;
    base_hash_ready_ = false;
}

bool PropagationStampRuntime::expandOneRound()
{
    if (!workblock_ || expanded_rounds_ >= kExpandRounds)
    {
        return false;
    }

    uint8_t round_number[3] = {};
    const std::size_t round_number_len =
        packRoundNumber(expanded_rounds_, round_number);
    uint8_t salt_material[reticulum::kFullHashSize + sizeof(round_number)] = {};
    std::memcpy(salt_material, transient_id_, sizeof(transient_id_));
    std::memcpy(salt_material + sizeof(transient_id_),
                round_number,
                round_number_len);
    uint8_t salt[reticulum::kFullHashSize] = {};
    reticulum::fullHash(salt_material,
                        sizeof(transient_id_) + round_number_len,
                        salt);

    uint8_t* output =
        workblock_ + static_cast<std::size_t>(expanded_rounds_) * kRoundBytes;
    if (!reticulum::hkdfSha256(transient_id_,
                               sizeof(transient_id_),
                               salt,
                               sizeof(salt),
                               nullptr,
                               0,
                               output,
                               kRoundBytes))
    {
        return false;
    }
    ++expanded_rounds_;
    return true;
}

bool PropagationStampRuntime::prepareSearch()
{
    if (!workblock_ || expanded_rounds_ != kExpandRounds)
    {
        return false;
    }
    if (::platform::esp::common::crypto::sha256_starts(&base_hash_, 0) != 0 ||
        ::platform::esp::common::crypto::sha256_update(
            &base_hash_, workblock_, kWorkblockBytes) != 0)
    {
        return false;
    }
    base_hash_ready_ = true;
    state_ = State::Searching;
    return true;
}

bool PropagationStampRuntime::searchOneRound()
{
    if (!base_hash_ready_)
    {
        return false;
    }

    esp_fill_random(stamp_, sizeof(stamp_));
    mbedtls_sha256_context attempt;
    mbedtls_sha256_init(&attempt);
    mbedtls_sha256_clone(&attempt, &base_hash_);
    uint8_t hash[reticulum::kFullHashSize] = {};
    const bool hashed = ::platform::esp::common::crypto::sha256_update(
                            &attempt, stamp_, sizeof(stamp_)) == 0 &&
                        ::platform::esp::common::crypto::sha256_finish(
                            &attempt, hash) == 0;
    mbedtls_sha256_free(&attempt);
    if (!hashed)
    {
        return false;
    }

    ++search_rounds_;
    if (meetsTarget(hash, target_cost_))
    {
        state_ = State::Complete;
    }
    return true;
}

bool PropagationStampRuntime::meetsTarget(
    const uint8_t hash[reticulum::kFullHashSize],
    uint8_t target_cost)
{
    if (!hash || target_cost == 0)
    {
        return hash != nullptr;
    }
    uint8_t remaining = target_cost;
    for (std::size_t index = 0;
         index < reticulum::kFullHashSize && remaining != 0;
         ++index)
    {
        const uint8_t bits = remaining >= 8 ? 8 : remaining;
        const uint8_t mask = static_cast<uint8_t>(0xFFU << (8U - bits));
        if ((hash[index] & mask) != 0)
        {
            return false;
        }
        remaining = static_cast<uint8_t>(remaining - bits);
    }
    return remaining == 0;
}

} // namespace chat::lxmf::runtime
