/**
 * @file vmp_control_runtime.cpp
 * @brief Pager-owned control-plane handoff for VMP v1.
 */

#include "platform/esp/arduino_common/voice/vmp_control_runtime.h"

#if defined(ARDUINO_T_LORA_PAGER) && defined(ARDUINO_LILYGO_LORA_LR1121)

#include "chat/infra/voice/vmp_control_ingress.h"
#include "platform/esp/arduino_common/app_tasks.h"

#include <cstring>

namespace platform::esp::arduino_common::voice::vmp_control
{
namespace
{

constexpr uint8_t kQueueDepth = 4U;
constexpr uint32_t kWorkerStackWords = 3072U;
constexpr UBaseType_t kWorkerPriority = 4U;

class Runtime final : public app::IRawRadioPacketInterceptor,
                      public chat::voice::vmp::IControlEnvelopeSink
{
  public:
    bool initialize()
    {
        if (initialized_)
        {
            return true;
        }
        ingress_.setSink(this);
        if (xTaskCreatePinnedToCore(&Runtime::workerEntry,
                                    "vmp_control",
                                    kWorkerStackWords,
                                    this,
                                    kWorkerPriority,
                                    &worker_task_,
                                    tskNO_AFFINITY) != pdPASS)
        {
            return false;
        }
        app::AppTasks::setRawRadioPacketInterceptor(this);
        initialized_ = true;
        return true;
    }

    void setHandler(EnvelopeHandler handler, void* context)
    {
        portENTER_CRITICAL(&lock_);
        handler_ = handler;
        handler_context_ = context;
        portEXIT_CRITICAL(&lock_);
    }

    bool tryConsume(const uint8_t* data,
                    std::size_t size,
                    float rssi,
                    float snr) override
    {
        chat::voice::vmp::ControlRxMetadata metadata{};
        metadata.rssi = rssi;
        metadata.snr = snr;
        return ingress_.tryConsume(data, size, metadata);
    }

    bool enqueueControl(const uint8_t* data,
                        std::size_t size,
                        const chat::voice::vmp::ControlRxMetadata& metadata) override
    {
        if (!data || size != kControlEnvelopeSize)
        {
            return false;
        }

        bool accepted = false;
        portENTER_CRITICAL(&lock_);
        if (queued_count_ < kQueueDepth)
        {
            Envelope& slot = slots_[write_index_];
            std::memcpy(slot.bytes, data, sizeof(slot.bytes));
            slot.rssi = metadata.rssi;
            slot.snr = metadata.snr;
            write_index_ = static_cast<uint8_t>((write_index_ + 1U) % kQueueDepth);
            ++queued_count_;
            accepted = true;
        }
        portEXIT_CRITICAL(&lock_);

        if (accepted && worker_task_)
        {
            xTaskNotifyGive(worker_task_);
        }
        return accepted;
    }

  private:
    static void workerEntry(void* context)
    {
        static_cast<Runtime*>(context)->runWorker();
    }

    void runWorker()
    {
        for (;;)
        {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            for (;;)
            {
                const Envelope* envelope = nullptr;
                EnvelopeHandler handler = nullptr;
                void* handler_context = nullptr;
                portENTER_CRITICAL(&lock_);
                if (queued_count_ != 0U)
                {
                    envelope = &slots_[read_index_];
                    handler = handler_;
                    handler_context = handler_context_;
                }
                portEXIT_CRITICAL(&lock_);
                if (!envelope)
                {
                    break;
                }

                if (handler)
                {
                    handler(*envelope, handler_context);
                }

                portENTER_CRITICAL(&lock_);
                if (queued_count_ != 0U)
                {
                    read_index_ = static_cast<uint8_t>((read_index_ + 1U) % kQueueDepth);
                    --queued_count_;
                }
                portEXIT_CRITICAL(&lock_);
            }
        }
    }

    chat::voice::vmp::ControlIngress ingress_{};
    Envelope slots_[kQueueDepth] = {};
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    uint8_t read_index_ = 0U;
    uint8_t write_index_ = 0U;
    uint8_t queued_count_ = 0U;
    EnvelopeHandler handler_ = nullptr;
    void* handler_context_ = nullptr;
    TaskHandle_t worker_task_ = nullptr;
    bool initialized_ = false;
};

Runtime s_runtime{};

} // namespace

bool initialize()
{
    return s_runtime.initialize();
}

void setEnvelopeHandler(EnvelopeHandler handler, void* context)
{
    s_runtime.setHandler(handler, context);
}

} // namespace platform::esp::arduino_common::voice::vmp_control

#else

namespace platform::esp::arduino_common::voice::vmp_control
{

bool initialize()
{
    return false;
}

void setEnvelopeHandler(EnvelopeHandler, void*)
{
}

} // namespace platform::esp::arduino_common::voice::vmp_control

#endif
