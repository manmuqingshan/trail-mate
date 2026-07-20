#pragma once

#include "platform/esp/arduino_common/chat/infra/store/protocol_chat_codec.h"

#include <cstddef>
#include <cstdint>

namespace chat::storage::v2
{

class FixedSlotJournalEngine final
{
  public:
    enum class State : uint8_t
    {
        Missing = 0,
        Ready,
        Incompatible,
        PartialTail,
        IoError,
    };

    struct Inspection
    {
        State state = State::Missing;
        uint32_t slot_count = 0;
    };

    Inspection inspect(const char* path,
                       MeshProtocol protocol,
                       JournalKind kind,
                       std::size_t slot_size) const;

    bool create(const char* path,
                MeshProtocol protocol,
                JournalKind kind,
                std::size_t slot_size) const;

    bool append(const char* path,
                MeshProtocol protocol,
                JournalKind kind,
                std::size_t slot_size,
                const void* slot) const;

    bool read(const char* path,
              MeshProtocol protocol,
              JournalKind kind,
              std::size_t slot_size,
              uint32_t slot_index,
              void* out_slot) const;

    static constexpr std::size_t headerSize();

  private:
    struct Header
    {
        uint32_t magic = 0;
        uint16_t schema = 0;
        uint8_t protocol = 0;
        uint8_t kind = 0;
        uint16_t slot_size = 0;
        uint16_t reserved = 0;
        uint32_t crc = 0;
    } __attribute__((packed));

    static constexpr uint32_t kMagic = 0x324C4E4AU; // JNL2

    static bool validDescriptor(MeshProtocol protocol,
                                JournalKind kind,
                                std::size_t slot_size);
    static Header makeHeader(MeshProtocol protocol,
                             JournalKind kind,
                             std::size_t slot_size);
    static bool matches(const Header& header,
                        MeshProtocol protocol,
                        JournalKind kind,
                        std::size_t slot_size);
};

bool replaceFileAtomically(const char* temp_path,
                           const char* final_path,
                           const char* backup_path);
bool recoverAtomicFile(const char* final_path,
                       const char* temp_path,
                       const char* backup_path);

constexpr std::size_t FixedSlotJournalEngine::headerSize()
{
    return sizeof(Header);
}

} // namespace chat::storage::v2
