/**
 * @file lxmf_memory.h
 * @brief Memory ownership helpers for embedded LXMF runtime buffers.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace chat::lxmf::runtime
{

[[noreturn]] inline void allocation_failed()
{
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
    throw std::bad_alloc();
#else
    std::abort();
#endif
}

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
            allocation_failed();
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
            allocation_failed();
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

using RuntimeByteBuffer = std::vector<uint8_t, PsramAllocator<uint8_t>>;
using RuntimeByteBufferList =
    std::vector<RuntimeByteBuffer, PsramAllocator<RuntimeByteBuffer>>;
using RuntimeByteSpanList =
    std::vector<::chat::lxmf::ByteSpan, PsramAllocator<::chat::lxmf::ByteSpan>>;
using RuntimeMapHash = std::array<uint8_t, 4>;
using RuntimeMapHashList =
    std::vector<RuntimeMapHash, PsramAllocator<RuntimeMapHash>>;
using ResourcePayloadBuffer = RuntimeByteBuffer;
using ResourcePayloadList = RuntimeByteBufferList;
using ResourceMetadataBuffer = RuntimeByteBuffer;
using ResourceBitmapBuffer = RuntimeByteBuffer;
using ResourceMapHashList = RuntimeMapHashList;
using PropagationIdList = RuntimeByteBufferList;
using PropagationMessageList = RuntimeByteBufferList;

inline bool appendRuntimeByteBufferCallback(const uint8_t* data,
                                            std::size_t len,
                                            void* context)
{
    auto* items = static_cast<RuntimeByteBufferList*>(context);
    if (!items || (!data && len != 0U))
    {
        return false;
    }

    RuntimeByteBuffer item;
    if (len != 0U)
    {
        item.assign(data, data + len);
    }
    items->push_back(std::move(item));
    return true;
}

inline RuntimeByteSpanList makeRuntimeByteSpans(
    const RuntimeByteBufferList& items)
{
    RuntimeByteSpanList spans;
    spans.reserve(items.size());
    for (const auto& item : items)
    {
        spans.push_back(::chat::lxmf::ByteSpan{item.data(), item.size()});
    }
    return spans;
}

inline ::chat::lxmf::ByteSpanList viewRuntimeByteSpans(
    const RuntimeByteSpanList& spans)
{
    return ::chat::lxmf::ByteSpanList{spans.data(), spans.size()};
}

} // namespace chat::lxmf::runtime
