#include "nrf52_node_startup_runtime.h"

#include <Arduino.h>

#include "nrf52_node_app_facade_runtime.h"
#include "nrf52_node_app_runtime_access.h"
#include "nrf52_node_target_board.h"
#include "nrf52_node_ui_runtime.h"
#include "platform/nrf52/debug/nrf52_debug_console.h"
#include "sys/clock.h"

namespace trailmate::apps::nrf52_node::startup_runtime
{

void run()
{
    platform::nrf52::debug_console::begin();
    platform::nrf52::debug_console::println();
    platform::nrf52::debug_console::printf("%s startup begin\n", target_board::kLogTag);
    auto& board = target_board::instance();
    platform::nrf52::debug_console::printf("%s startup board begin\n", target_board::kLogTag);
    (void)board.begin();
    platform::nrf52::debug_console::printf("%s startup board ok\n", target_board::kLogTag);
    sys::set_millis_provider([]() -> uint32_t
                             { return millis(); });
    sys::set_epoch_seconds_provider([]() -> uint32_t
                                    { return target_board::instance().currentEpochSeconds(); });
    sys::set_sleep_provider([](uint32_t ms)
                            { delay(ms); });
    platform::nrf52::debug_console::printf("%s startup ui begin\n", target_board::kLogTag);
    ui_runtime::initialize();
    platform::nrf52::debug_console::printf("%s startup ui ok\n", target_board::kLogTag);
    ui_runtime::appendBootLog("startup begin");
    ui_runtime::appendBootLog("board/input ok");

    platform::nrf52::debug_console::printf("%s startup radio bind\n", target_board::kLogTag);
    (void)board.bindRadioIo();
    const bool lora_ok = board.beginRadioIo();
    platform::nrf52::debug_console::printf("%s startup lora io %s\n", target_board::kLogTag, lora_ok ? "ok" : "failed");
    ui_runtime::appendBootLog(lora_ok ? "lora io ok" : "lora io fail");

    if (app_runtime_access::initialize())
    {
        auto& cfg = AppFacadeRuntime::instance().getConfig();
        ui_runtime::bindChatObservers();
        (void)board.startGpsRuntime(cfg);
        platform::nrf52::debug_console::printf("%s startup app facade ok\n", target_board::kLogTag);
        ui_runtime::appendBootLog("app/gps ok");
        char freq_text[20] = {};
        if (board.formatLoraFrequencyMHz(board.activeLoraFrequencyHz(), freq_text, sizeof(freq_text)))
        {
            ui_runtime::appendBootLog(freq_text);
        }
        ui_runtime::appendBootLog(cfg.mesh_protocol == chat::MeshProtocol::MeshCore ? "proto MC" : "proto MT");
    }
    else
    {
        platform::nrf52::debug_console::printf("%s startup app facade failed\n", target_board::kLogTag);
        ui_runtime::appendBootLog("app init fail");
    }
}

} // namespace trailmate::apps::nrf52_node::startup_runtime
