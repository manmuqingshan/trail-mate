/*
  memtools.h
  June 2019

  Tools for looking at memory on the stm32.  See also debug_alloc.h
*/

#include <stdlib.h>
#include <sys/types.h>
#include <math.h>
#include "memtools.h"

#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK)
#include <esp_heap_caps.h>
#endif

/*
 * Pager and T-Deck Codec2 state and DSP scratch are non-DMA data. Keep them
 * in PSRAM so the fragmented internal heap remains available for FreeRTOS
 * stacks and I2S DMA. A missing PSRAM allocation fails Codec2 creation safely
 * instead of silently consuming scarce internal SRAM.
 */
void* codec2_malloc(size_t size)
{
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK)
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

void* codec2_calloc(size_t nmemb, size_t size)
{
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK)
    return heap_caps_calloc(nmemb, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(nmemb, size);
#endif
}

void codec2_free(void* ptr)
{
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK)
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}
