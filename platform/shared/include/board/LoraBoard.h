#pragma once

#include <cstddef>
#include <cstdint>

// Shared LoRa board capability contract.
class LoraBoard
{
  public:
    virtual ~LoraBoard() = default;

    virtual bool isRadioOnline() const = 0;

    // Synchronous contract: returns only after the packet transmission has
    // completed or failed. Implementations must not hold a shared display/SD
    // SPI bus lock while merely waiting for the air-time TX-done interrupt.
    virtual int transmitRadio(const uint8_t* data, size_t len) = 0;
    virtual int startRadioReceive() = 0;
    virtual uint32_t getRadioIrqFlags() = 0;
    virtual int getRadioPacketLength(bool update) = 0;
    virtual int readRadioData(uint8_t* buf, size_t len) = 0;
    virtual void clearRadioIrqFlags(uint32_t flags) = 0;
    virtual float getRadioRSSI() = 0;
    virtual float getRadioInstantRSSI()
    {
        return getRadioRSSI();
    }
    virtual float getRadioSNR() = 0;

    // Board-specific LoRa configuration without exposing SX126x types.
    // Returns zero only when every required receive parameter was accepted.
    virtual int configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                   int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                   uint8_t crc_len) = 0;

    // Put the radio hardware in a state that cannot access the shared SPI
    // bus while an external owner (for example USB MSC) owns the SD card.
    // Boards must opt in explicitly; the conservative default rejects the
    // external-storage session when an online radio has no such guarantee.
    virtual bool quiesceForExternalStorage()
    {
        return !isRadioOnline();
    }
};
