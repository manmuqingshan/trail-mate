#pragma once

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_8BIT 0x00000004u
#define MALLOC_CAP_DMA 0x00000008u
#define MALLOC_CAP_INTERNAL 0x00000010u
#define MALLOC_CAP_SPIRAM 0x00000020u

#ifdef __cplusplus
extern "C"
{
#endif

    size_t heap_caps_get_free_size(uint32_t caps);
    size_t heap_caps_get_minimum_free_size(uint32_t caps);

#ifdef __cplusplus
}
#endif
