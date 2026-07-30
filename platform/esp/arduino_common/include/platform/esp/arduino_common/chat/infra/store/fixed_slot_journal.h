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

    enum class ReadStatus : uint8_t
    {
        Ok = 0,
        InvalidArgument,
        OutOfRange,
        Unavailable,
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

    ReadStatus readStatus(const char* path,
                          MeshProtocol protocol,
                          JournalKind kind,
                          std::size_t slot_size,
                          const Inspection& inspection,
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

// A cursor performs at most one bounded slot read per next() call. It keeps
// only metadata between calls, so no filesystem handle or logical store lock
// is held while the owner is waiting for the next maintenance tick.
class FixedSlotJournalCursor final
{
  public:
    enum class StepStatus : uint8_t
    {
        Item = 0,
        Complete,
        Missing,
        Invalid,
        Unavailable,
    };

    bool begin(const FixedSlotJournalEngine& engine,
               const char* path,
               MeshProtocol protocol,
               JournalKind kind,
               std::size_t slot_size);

    StepStatus next(const FixedSlotJournalEngine& engine,
                    void* out_slot,
                    std::size_t out_len);

    bool seek(uint32_t slot_index);
    void reset();
    const FixedSlotJournalEngine::Inspection& inspection() const
    {
        return inspection_;
    }
    uint32_t nextIndex() const { return next_index_; }
    std::size_t slotSize() const { return slot_size_; }
    bool active() const { return active_; }

  private:
    char path_[160] = {};
    MeshProtocol protocol_ = MeshProtocol::Meshtastic;
    JournalKind kind_ = JournalKind::MessageSegment;
    std::size_t slot_size_ = 0U;
    FixedSlotJournalEngine::Inspection inspection_{};
    uint32_t next_index_ = 0U;
    bool active_ = false;
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
