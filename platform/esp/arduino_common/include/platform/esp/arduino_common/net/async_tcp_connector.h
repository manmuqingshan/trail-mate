#pragma once

#include <cstdint>

namespace platform::esp::arduino_common::net
{

enum class TcpConnectPhase : std::uint8_t
{
    Idle = 0,
    Resolving,
    Connecting,
    Connected,
    Failed,
};

enum class TcpConnectFailure : std::uint8_t
{
    None = 0,
    InvalidEndpoint,
    ResolverBusy,
    ResolveFailed,
    ResolveEmptyResponse,
    SocketOpenFailed,
    ConnectFailed,
    TimedOut,
    Cancelled,
};

struct TcpConnectStatus
{
    TcpConnectPhase phase = TcpConnectPhase::Idle;
    TcpConnectFailure failure = TcpConnectFailure::None;
    int detail = 0;
};

/**
 * A bounded, task-free DNS + TCP connect state machine.
 *
 * DNS completion storage lives in a process-lifetime broker, so cancelling or
 * destroying a connector cannot leave lwIP with a dangling callback pointer.
 * poll() never waits: callers can safely invoke it from an LVGL/runtime tick.
 */
class AsyncTcpConnector final
{
  public:
    AsyncTcpConnector() = default;
    ~AsyncTcpConnector();

    AsyncTcpConnector(const AsyncTcpConnector&) = delete;
    AsyncTcpConnector& operator=(const AsyncTcpConnector&) = delete;

    bool start(const char* host,
               std::uint16_t port,
               std::uint32_t now_ms,
               std::uint32_t timeout_ms);
    TcpConnectStatus poll(std::uint32_t now_ms);
    void cancel();

    TcpConnectStatus status() const { return status_; }
    bool pending() const;
    int takeSocket();
    int takeNonBlockingSocket();

  private:
    bool beginSocket(std::uint32_t ipv4_address, std::uint32_t now_ms);
    void fail(TcpConnectFailure failure, int detail);

    TcpConnectStatus status_{};
    int socket_ = -1;
    int dns_slot_ = -1;
    std::uint32_t dns_generation_ = 0;
    std::uint32_t deadline_ms_ = 0;
    std::uint32_t timeout_ms_ = 0;
    std::uint16_t port_ = 0;
};

const char* tcpConnectFailureName(TcpConnectFailure failure);

} // namespace platform::esp::arduino_common::net
