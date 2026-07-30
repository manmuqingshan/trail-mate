#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::common::memory
{

struct HeapSnapshot
{
    std::size_t internal_free = 0;
    std::size_t internal_largest = 0;
    std::size_t dma_free = 0;
    std::size_t dma_largest = 0;
    std::size_t psram_free = 0;
    std::size_t psram_largest = 0;
    std::size_t minimum_internal_free = 0;
    std::size_t minimum_dma_free = 0;
    std::size_t minimum_psram_free = 0;
};

HeapSnapshot capture();

bool meetsFloor(const HeapSnapshot& snapshot,
                std::size_t required_internal,
                std::size_t required_dma,
                std::size_t required_psram,
                std::size_t internal_floor,
                std::size_t dma_floor,
                std::size_t psram_floor = 0);

bool admit(const char* owner,
           std::size_t required_internal,
           std::size_t required_dma,
           std::size_t required_psram,
           std::size_t internal_floor,
           std::size_t dma_floor,
           std::size_t psram_floor = 0);

void* allocatePreferred(const char* owner,
                        std::size_t bytes,
                        bool allow_internal_fallback = true);

void logSnapshot(const char* owner, const char* stage);

} // namespace platform::esp::common::memory
