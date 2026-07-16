/**
 * @file lxmf_memory.h
 * @brief Memory ownership helpers for embedded LXMF runtime buffers.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#else
#include <cstdlib>
#endif

namespace chat::lxmf::runtime
{

template <typename T>
class PsramAllocator
{
  public:
    using value_type = T;

    PsramAllocator() noexcept = default;

    template <typename U>
    PsramAllocator(const PsramAllocator<U>&) noexcept
    {
    }

    T* allocate(std::size_t count)
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
        {
            throw std::bad_alloc();
        }

        const std::size_t bytes = count * sizeof(T);
        if (bytes == 0)
        {
            return nullptr;
        }

#if defined(ESP_PLATFORM)
        void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        void* ptr = std::malloc(bytes);
#endif
        if (!ptr)
        {
            throw std::bad_alloc();
        }
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept
    {
        if (!ptr)
        {
            return;
        }
#if defined(ESP_PLATFORM)
        heap_caps_free(ptr);
#else
        std::free(ptr);
#endif
    }

    template <typename U>
    struct rebind
    {
        using other = PsramAllocator<U>;
    };
};

template <typename T, typename U>
bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept
{
    return true;
}

template <typename T, typename U>
bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept
{
    return false;
}

using ResourcePayloadBuffer = std::vector<uint8_t, PsramAllocator<uint8_t>>;
using ResourcePayloadList = std::vector<ResourcePayloadBuffer>;

} // namespace chat::lxmf::runtime
