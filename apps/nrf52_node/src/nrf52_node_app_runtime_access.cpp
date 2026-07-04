#include "nrf52_node_app_runtime_access.h"

#include <Arduino.h>
#include <cstdlib>

#include "app/app_facade_access.h"
#include "chat/ports/i_mesh_adapter.h"
#include "nrf52_node_app_facade_runtime.h"
#include "nrf52_node_target_board.h"
#include "nrf52_node_ui_runtime.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/debug/nrf52_debug_console.h"

extern "C" bool trailmate_nrf52_debug_check_gps_guard(const char* tag)
{
    return trailmate::apps::nrf52_node::target_board::instance().debugCheckGpsMemoryGuard(tag);
}

namespace trailmate::apps::nrf52_node::app_runtime_access
{
namespace
{

Status s_status{};
uint32_t s_rx_packet_count = 0;
uint32_t s_last_rx_log_ms = 0;

int decimalDigit(int value)
{
    return value < 0 ? -value : value;
}

} // namespace

bool initialize()
{
    if (s_status.initialized)
    {
        return s_status.app_facade_bound;
    }

    s_status = Status{};
    s_status.initialized = true;

    AppFacadeRuntime& runtime = AppFacadeRuntime::instance();
    s_status.app_facade_bound = runtime.initialize() && app::hasAppFacade();
    if (!s_status.app_facade_bound)
    {
        platform::nrf52::debug_console::printf("%s app runtime init failed\n", target_board::kLogTag);
    }
    return s_status.app_facade_bound;
}

void tick()
{
    auto& board = target_board::instance();
    board.tickGps();
    (void)trailmate_nrf52_debug_check_gps_guard("app_tick_after_gps");
    target_board::BoardInputEvent input_event{};
    (void)board.pollInputEvent(&input_event);
    AppFacadeRuntime& runtime = AppFacadeRuntime::instance();
    if (chat::IMeshAdapter* adapter = runtime.getMeshAdapter())
    {
        adapter->processSendQueue();
        (void)trailmate_nrf52_debug_check_gps_guard("app_tick_after_send_queue");

        platform::nrf52::arduino_common::chat::infra::RadioPacket packet{};
        auto* io = platform::nrf52::arduino_common::chat::infra::radioPacketIo();
        while (io && io->pollReceive(&packet))
        {
            ++s_rx_packet_count;
            adapter->setLastRxStats(packet.rx_meta.rssi_dbm_x10 / 10.0f,
                                    packet.rx_meta.snr_db_x10 / 10.0f);
            const uint32_t now_ms = millis();
            if (s_rx_packet_count <= 4 || (now_ms - s_last_rx_log_ms) >= 2000U)
            {
                s_last_rx_log_ms = now_ms;
                platform::nrf52::debug_console::printf("%s rx raw #%lu len=%u rssi=%d.%01d snr=%d.%01d\n",
                                                       target_board::kLogTag,
                                                       static_cast<unsigned long>(s_rx_packet_count),
                                                       static_cast<unsigned>(packet.size),
                                                       packet.rx_meta.rssi_dbm_x10 / 10,
                                                       decimalDigit(packet.rx_meta.rssi_dbm_x10 % 10),
                                                       packet.rx_meta.snr_db_x10 / 10,
                                                       decimalDigit(packet.rx_meta.snr_db_x10 % 10));
            }
            adapter->handleRawPacket(packet.data, packet.size);
            (void)trailmate_nrf52_debug_check_gps_guard("app_tick_after_raw_packet");
        }
    }

    runtime.updateCoreServices();
    (void)trailmate_nrf52_debug_check_gps_guard("app_tick_after_core_services");
    runtime.tickEventRuntime();
    (void)trailmate_nrf52_debug_check_gps_guard("app_tick_after_event_runtime");
    runtime.dispatchPendingEvents();
    (void)trailmate_nrf52_debug_check_gps_guard("app_tick_after_events");
    ui_runtime::tick(&input_event);
    (void)trailmate_nrf52_debug_check_gps_guard("app_tick_after_ui");
}

const Status& status()
{
    return s_status;
}

} // namespace trailmate::apps::nrf52_node::app_runtime_access
