/*
  Minimal vendored variant for LilyGO T-Echo-Lite nRF52840.
  Source basis: LilyGO T-Echo-Lite public repository, tools/win10 vscode platformio start/variants/t_echo_lite_nrf52840/variant.cpp
*/
#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    pinMode(PIN_LED1, OUTPUT);
    ledOff(PIN_LED1);
    pinMode(PIN_LED2, OUTPUT);
    ledOff(PIN_LED2);
}
