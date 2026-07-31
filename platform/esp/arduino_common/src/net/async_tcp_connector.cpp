#include "platform/esp/arduino_common/net/async_tcp_connector.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <lwip/dns.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <freertos/FreeRTOS.h>

namespace platform::esp::arduino_common::net
{
namespace
{

constexpr std::size_t kDnsSlotCount = 4;

struct DnsSlot
{
    bool in_use = false;
    bool abandoned = false;
    bool complete = false;
    std::uint32_t generation = 0;
    std::uint32_t address = 0;
    int error = 0;
};

std::array<DnsSlot, kDnsSlotCount> s_dns_slots{};
portMUX_TYPE s_dns_lock = portMUX_INITIALIZER_UNLOCKED;
std::uint32_t s_dns_generation = 1;

void log_resolved_ipv4(const char* host, const ip_addr_t* address, const char* source)
{
    char ipv4_text[16] = {};
    if (!address || !IP_IS_V4(address) ||
        !ip4addr_ntoa_r(ip_2_ip4(address), ipv4_text, sizeof(ipv4_text)))
    {
        std::printf("[DNS] result host=%s source=%s has_no_ipv4\n",
                    host ? host : "<unknown>",
                    source ? source : "unknown");
        return;
    }

    std::printf("[DNS] resolved host=%s ip=%s source=%s\n",
                host ? host : "<unknown>",
                ipv4_text,
                source ? source : "unknown");
}

void release_slot_locked(DnsSlot& slot)
{
    slot = {};
}

void complete_dns_slot(DnsSlot& slot, const ip_addr_t* address, int error)
{
    portENTER_CRITICAL(&s_dns_lock);
    if (slot.in_use)
    {
        if (slot.abandoned)
        {
            release_slot_locked(slot);
        }
        else
        {
            slot.complete = true;
            if (error == 0 && address && IP_IS_V4(address))
            {
                slot.address = ip4_addr_get_u32(ip_2_ip4(address));
                slot.error = 0;
            }
            else
            {
                slot.address = 0;
                slot.error = error != 0 ? error : ERR_VAL;
            }
        }
    }
    portEXIT_CRITICAL(&s_dns_lock);
}

void dns_found(const char* host, const ip_addr_t* address, void* arg)
{
    auto* slot = static_cast<DnsSlot*>(arg);
    if (!slot)
    {
        return;
    }
    // lwIP reports NXDOMAIN and malformed/empty replies through a null
    // address in the completion callback.  Preserve that distinction so a
    // caller can diagnose resolver failure without treating it as TCP I/O.
    log_resolved_ipv4(host, address, "network");
    complete_dns_slot(*slot, address, address ? 0 : EHOSTUNREACH);
}

int claim_dns_slot(std::uint32_t& generation)
{
    int index = -1;
    portENTER_CRITICAL(&s_dns_lock);
    for (std::size_t i = 0; i < s_dns_slots.size(); ++i)
    {
        if (s_dns_slots[i].in_use)
        {
            continue;
        }
        ++s_dns_generation;
        if (s_dns_generation == 0)
        {
            s_dns_generation = 1;
        }
        s_dns_slots[i] = {};
        s_dns_slots[i].in_use = true;
        s_dns_slots[i].generation = s_dns_generation;
        generation = s_dns_generation;
        index = static_cast<int>(i);
        break;
    }
    portEXIT_CRITICAL(&s_dns_lock);
    return index;
}

void abandon_dns_slot(int index, std::uint32_t generation)
{
    if (index < 0 || static_cast<std::size_t>(index) >= s_dns_slots.size())
    {
        return;
    }
    portENTER_CRITICAL(&s_dns_lock);
    DnsSlot& slot = s_dns_slots[static_cast<std::size_t>(index)];
    if (slot.in_use && slot.generation == generation)
    {
        if (slot.complete)
        {
            release_slot_locked(slot);
        }
        else
        {
            slot.abandoned = true;
        }
    }
    portEXIT_CRITICAL(&s_dns_lock);
}

void release_dns_slot(int index, std::uint32_t generation)
{
    if (index < 0 || static_cast<std::size_t>(index) >= s_dns_slots.size())
    {
        return;
    }
    portENTER_CRITICAL(&s_dns_lock);
    DnsSlot& slot = s_dns_slots[static_cast<std::size_t>(index)];
    if (slot.in_use && slot.generation == generation)
    {
        release_slot_locked(slot);
    }
    portEXIT_CRITICAL(&s_dns_lock);
}

bool take_dns_result(int index,
                     std::uint32_t generation,
                     bool& complete,
                     std::uint32_t& address,
                     int& error)
{
    complete = false;
    if (index < 0 || static_cast<std::size_t>(index) >= s_dns_slots.size())
    {
        return false;
    }

    bool valid = false;
    portENTER_CRITICAL(&s_dns_lock);
    DnsSlot& slot = s_dns_slots[static_cast<std::size_t>(index)];
    if (slot.in_use && slot.generation == generation && !slot.abandoned)
    {
        valid = true;
        complete = slot.complete;
        if (complete)
        {
            address = slot.address;
            error = slot.error;
            release_slot_locked(slot);
        }
    }
    portEXIT_CRITICAL(&s_dns_lock);
    return valid;
}

bool deadline_elapsed(std::uint32_t now_ms, std::uint32_t deadline_ms)
{
    return deadline_ms != 0 &&
           static_cast<std::int32_t>(now_ms - deadline_ms) >= 0;
}

} // namespace

AsyncTcpConnector::~AsyncTcpConnector()
{
    cancel();
}

bool AsyncTcpConnector::start(const char* host,
                              std::uint16_t port,
                              std::uint32_t now_ms,
                              std::uint32_t timeout_ms)
{
    cancel();
    if (!host || host[0] == '\0' || port == 0 || timeout_ms == 0)
    {
        fail(TcpConnectFailure::InvalidEndpoint, EINVAL);
        return false;
    }
    if (std::strlen(host) >= DNS_MAX_NAME_LENGTH)
    {
        fail(TcpConnectFailure::InvalidEndpoint, ENAMETOOLONG);
        return false;
    }

    port_ = port;
    timeout_ms_ = timeout_ms;
    deadline_ms_ = now_ms + timeout_ms;

    ip4_addr_t parsed{};
    if (ip4addr_aton(host, &parsed) != 0)
    {
        return beginSocket(ip4_addr_get_u32(&parsed), now_ms);
    }

    dns_slot_ = claim_dns_slot(dns_generation_);
    if (dns_slot_ < 0)
    {
        fail(TcpConnectFailure::ResolverBusy, ERR_MEM);
        return false;
    }

    ip_addr_t resolved{};
    const err_t err = dns_gethostbyname(
        host,
        &resolved,
        dns_found,
        &s_dns_slots[static_cast<std::size_t>(dns_slot_)]);
    if (err == ERR_OK)
    {
        // A cached answer completes synchronously and never calls dns_found().
        release_dns_slot(dns_slot_, dns_generation_);
        dns_slot_ = -1;
        if (!IP_IS_V4(&resolved))
        {
            fail(TcpConnectFailure::ResolveFailed, ERR_VAL);
            return false;
        }
        log_resolved_ipv4(host, &resolved, "cache");
        return beginSocket(ip4_addr_get_u32(ip_2_ip4(&resolved)), now_ms);
    }
    if (err != ERR_INPROGRESS)
    {
        release_dns_slot(dns_slot_, dns_generation_);
        dns_slot_ = -1;
        fail(TcpConnectFailure::ResolveFailed, err);
        return false;
    }
    status_.phase = TcpConnectPhase::Resolving;
    status_.failure = TcpConnectFailure::None;
    status_.detail = 0;
    return true;
}

TcpConnectStatus AsyncTcpConnector::poll(std::uint32_t now_ms)
{
    if (status_.phase == TcpConnectPhase::Resolving)
    {
        if (deadline_elapsed(now_ms, deadline_ms_))
        {
            abandon_dns_slot(dns_slot_, dns_generation_);
            dns_slot_ = -1;
            fail(TcpConnectFailure::TimedOut, ETIMEDOUT);
            return status_;
        }

        bool complete = false;
        std::uint32_t address = 0;
        int error = 0;
        if (!take_dns_result(dns_slot_,
                             dns_generation_,
                             complete,
                             address,
                             error))
        {
            dns_slot_ = -1;
            fail(TcpConnectFailure::ResolveFailed, ERR_VAL);
            return status_;
        }
        if (!complete)
        {
            return status_;
        }
        dns_slot_ = -1;
        if (error != 0 || address == 0)
        {
            fail(error == EHOSTUNREACH
                     ? TcpConnectFailure::ResolveEmptyResponse
                     : TcpConnectFailure::ResolveFailed,
                 error);
            return status_;
        }
        deadline_ms_ = now_ms + timeout_ms_;
        (void)beginSocket(address, now_ms);
    }

    if (status_.phase != TcpConnectPhase::Connecting)
    {
        return status_;
    }
    if (deadline_elapsed(now_ms, deadline_ms_))
    {
        fail(TcpConnectFailure::TimedOut, ETIMEDOUT);
        return status_;
    }

    fd_set writable;
    fd_set failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(socket_, &writable);
    FD_SET(socket_, &failed);
    timeval timeout{};
    const int selected =
        select(socket_ + 1, nullptr, &writable, &failed, &timeout);
    if (selected < 0)
    {
        fail(TcpConnectFailure::ConnectFailed, errno);
        return status_;
    }
    if (selected == 0)
    {
        return status_;
    }

    int socket_error = 0;
    socklen_t error_len = sizeof(socket_error);
    if (getsockopt(socket_,
                   SOL_SOCKET,
                   SO_ERROR,
                   &socket_error,
                   &error_len) != 0 ||
        socket_error != 0 ||
        FD_ISSET(socket_, &failed))
    {
        fail(TcpConnectFailure::ConnectFailed,
             socket_error != 0 ? socket_error : errno);
        return status_;
    }

    status_.phase = TcpConnectPhase::Connected;
    status_.failure = TcpConnectFailure::None;
    status_.detail = 0;
    return status_;
}

void AsyncTcpConnector::cancel()
{
    if (dns_slot_ >= 0)
    {
        abandon_dns_slot(dns_slot_, dns_generation_);
    }
    dns_slot_ = -1;
    dns_generation_ = 0;
    if (socket_ >= 0)
    {
        close(socket_);
    }
    socket_ = -1;
    port_ = 0;
    deadline_ms_ = 0;
    timeout_ms_ = 0;
    status_ = {};
}

bool AsyncTcpConnector::pending() const
{
    return status_.phase == TcpConnectPhase::Resolving ||
           status_.phase == TcpConnectPhase::Connecting;
}

int AsyncTcpConnector::takeSocket()
{
    if (status_.phase != TcpConnectPhase::Connected || socket_ < 0)
    {
        return -1;
    }
    const int flags = fcntl(socket_, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_, F_SETFL, flags & ~O_NONBLOCK) < 0)
    {
        fail(TcpConnectFailure::SocketOpenFailed, errno);
        return -1;
    }
    const int socket = socket_;
    socket_ = -1;
    status_ = {};
    deadline_ms_ = 0;
    timeout_ms_ = 0;
    return socket;
}

int AsyncTcpConnector::takeNonBlockingSocket()
{
    if (status_.phase != TcpConnectPhase::Connected || socket_ < 0)
    {
        return -1;
    }
    const int socket = socket_;
    socket_ = -1;
    status_ = {};
    deadline_ms_ = 0;
    timeout_ms_ = 0;
    return socket;
}

bool AsyncTcpConnector::beginSocket(std::uint32_t ipv4_address,
                                    std::uint32_t)
{
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ < 0)
    {
        fail(TcpConnectFailure::SocketOpenFailed, errno);
        return false;
    }

    const int flags = fcntl(socket_, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        fail(TcpConnectFailure::SocketOpenFailed, errno);
        return false;
    }

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port_);
    endpoint.sin_addr.s_addr = ipv4_address;
    const int result =
        lwip_connect(socket_,
                     reinterpret_cast<const sockaddr*>(&endpoint),
                     sizeof(endpoint));
    if (result == 0)
    {
        status_.phase = TcpConnectPhase::Connected;
        status_.failure = TcpConnectFailure::None;
        status_.detail = 0;
        return true;
    }
    if (errno != EINPROGRESS)
    {
        fail(TcpConnectFailure::ConnectFailed, errno);
        return false;
    }

    status_.phase = TcpConnectPhase::Connecting;
    status_.failure = TcpConnectFailure::None;
    status_.detail = 0;
    return true;
}

void AsyncTcpConnector::fail(TcpConnectFailure failure, int detail)
{
    if (socket_ >= 0)
    {
        close(socket_);
    }
    socket_ = -1;
    status_.phase = TcpConnectPhase::Failed;
    status_.failure = failure;
    status_.detail = detail;
}

const char* tcpConnectFailureName(TcpConnectFailure failure)
{
    switch (failure)
    {
    case TcpConnectFailure::None:
        return "none";
    case TcpConnectFailure::InvalidEndpoint:
        return "invalid_endpoint";
    case TcpConnectFailure::ResolverBusy:
        return "resolver_busy";
    case TcpConnectFailure::ResolveFailed:
        return "resolve_failed";
    case TcpConnectFailure::ResolveEmptyResponse:
        return "resolve_empty_response";
    case TcpConnectFailure::SocketOpenFailed:
        return "socket_open_failed";
    case TcpConnectFailure::ConnectFailed:
        return "connect_failed";
    case TcpConnectFailure::TimedOut:
        return "timed_out";
    case TcpConnectFailure::Cancelled:
        return "cancelled";
    default:
        return "unknown";
    }
}

} // namespace platform::esp::arduino_common::net
