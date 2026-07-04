#pragma once

#include <stdint.h>

#define MESH_SERVICE_UUID "6ba1b218-15a8-461f-9fa8-5dcae273eafd"
#define TORADIO_UUID "f75c76d2-129e-4dad-a1dd-7866124401e7"
#define FROMRADIO_UUID "2c55e69e-4993-11ed-b878-0242ac120002"
#define FROMNUM_UUID "ed9da18c-a800-4f66-a670-aa7547e34453"
#define LOGRADIO_UUID "5a3d6e49-06e6-4423-9944-e9de8cdf9547"
#define FROMRADIOSYNC_UUID "888a50c3-982d-45db-9963-c7923769165d"

// Bluefruit expects 128-bit UUID bytes in little-endian order on nRF52.
static constexpr uint8_t MESH_SERVICE_UUID_128_LE[16] = {
    0xfd,
    0xea,
    0x73,
    0xe2,
    0xca,
    0x5d,
    0xa8,
    0x9f,
    0x1f,
    0x46,
    0xa8,
    0x15,
    0x18,
    0xb2,
    0xa1,
    0x6b,
};
static constexpr uint8_t TORADIO_UUID_128_LE[16] = {
    0xe7,
    0x01,
    0x44,
    0x12,
    0x66,
    0x78,
    0xdd,
    0xa1,
    0xad,
    0x4d,
    0x9e,
    0x12,
    0xd2,
    0x76,
    0x5c,
    0xf7,
};
static constexpr uint8_t FROMRADIO_UUID_128_LE[16] = {
    0x02,
    0x00,
    0x12,
    0xac,
    0x42,
    0x02,
    0x78,
    0xb8,
    0xed,
    0x11,
    0x93,
    0x49,
    0x9e,
    0xe6,
    0x55,
    0x2c,
};
static constexpr uint8_t FROMNUM_UUID_128_LE[16] = {
    0x53,
    0x44,
    0xe3,
    0x47,
    0x75,
    0xaa,
    0x70,
    0xa6,
    0x66,
    0x4f,
    0x00,
    0xa8,
    0x8c,
    0xa1,
    0x9d,
    0xed,
};
static constexpr uint8_t LOGRADIO_UUID_128_LE[16] = {
    0x47,
    0x95,
    0xdf,
    0x8c,
    0xde,
    0xe9,
    0x44,
    0x99,
    0x23,
    0x44,
    0xe6,
    0x06,
    0x49,
    0x6e,
    0x3d,
    0x5a,
};

#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
