#include "platform/ui/http_client_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)

#include "platform/esp/idf_common/wireless_companion/c6_companion.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace platform::ui::http_client
{
namespace
{

using ::platform::esp::idf_common::wireless_companion::WifiTcpState;
using ::platform::esp::idf_common::wireless_companion::WifiTcpTransport;

constexpr const char* kTag = "p4_http";
constexpr std::size_t kDefaultBufferBytes = 1024;
constexpr std::size_t kMaxResponseHeaderBytes = 16 * 1024;
constexpr std::size_t kMaxChunkLineBytes = 1024;
constexpr unsigned kMaxRedirects = 4;
constexpr std::uint32_t kPollDelayMs = 5;

std::mutex s_http_mutex;

struct ParsedUrl
{
    bool tls = false;
    std::string host;
    std::string path;
    std::uint16_t port = 0;
};

struct ResponseHead
{
    int status = 0;
    std::int64_t content_length = -1;
    bool chunked = false;
    std::string location;
};

class HeapBuffer
{
  public:
    ~HeapBuffer()
    {
        if (data_)
        {
            heap_caps_free(data_);
        }
    }

    HeapBuffer(const HeapBuffer&) = delete;
    HeapBuffer& operator=(const HeapBuffer&) = delete;
    HeapBuffer() = default;

    bool allocate(std::size_t bytes, bool prefer_psram)
    {
        const std::uint32_t primary = prefer_psram
                                          ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                                          : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        const std::uint32_t secondary = prefer_psram
                                            ? MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
                                            : MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        data_ = static_cast<std::uint8_t*>(
            heap_caps_malloc_prefer(bytes, 2, primary, secondary));
        size_ = data_ ? bytes : 0;
        return data_ != nullptr;
    }

    std::uint8_t* data()
    {
        return data_;
    }

    std::size_t size() const
    {
        return size_;
    }

  private:
    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

std::int64_t deadline_after_ms(int timeout_ms)
{
    const int effective_timeout = timeout_ms > 0 ? timeout_ms : 30000;
    return esp_timer_get_time() + static_cast<std::int64_t>(effective_timeout) * 1000;
}

bool before_deadline(std::int64_t deadline_us)
{
    return esp_timer_get_time() < deadline_us;
}

void poll_delay()
{
    vTaskDelay(pdMS_TO_TICKS(kPollDelayMs));
}

bool request_prefers_psram(const Request& request)
{
    return request.client == wifi_access::Client::RouteStorage ||
           request.client == wifi_access::Client::PackRepository;
}

bool memory_ready(const Request& request)
{
    const std::uint32_t free_bytes = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::uint32_t largest_bytes = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return free_bytes >= request.min_internal_free_bytes &&
           largest_bytes >= request.min_internal_largest_bytes;
}

bool validate_request(const Request& request, std::string& out_error)
{
    if (!request.url || request.url[0] == '\0')
    {
        out_error = "Missing HTTP URL";
        return false;
    }
    if (request.client == wifi_access::Client::Unknown)
    {
        out_error = "Missing Wi-Fi access client";
        return false;
    }
    if (request.access_kind != wifi_access::AccessKind::HttpMetadata &&
        request.access_kind != wifi_access::AccessKind::HttpDownload &&
        request.access_kind != wifi_access::AccessKind::OtaDownload)
    {
        out_error = "Invalid HTTP access kind";
        return false;
    }
    if (!memory_ready(request))
    {
        out_error = "Low HTTP memory";
        return false;
    }
    return true;
}

bool parse_port(std::string_view text, std::uint16_t& out)
{
    if (text.empty())
    {
        return false;
    }
    unsigned value = 0;
    for (char c : text)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            return false;
        }
        value = value * 10U + static_cast<unsigned>(c - '0');
        if (value > UINT16_MAX)
        {
            return false;
        }
    }
    if (value == 0)
    {
        return false;
    }
    out = static_cast<std::uint16_t>(value);
    return true;
}

bool parse_url(std::string_view url, ParsedUrl& out, std::string& error)
{
    constexpr std::string_view kHttp = "http://";
    constexpr std::string_view kHttps = "https://";
    if (url.starts_with(kHttps))
    {
        out.tls = true;
        out.port = 443;
        url.remove_prefix(kHttps.size());
    }
    else if (url.starts_with(kHttp))
    {
        out.tls = false;
        out.port = 80;
        url.remove_prefix(kHttp.size());
    }
    else
    {
        error = "Unsupported HTTP URL scheme";
        return false;
    }

    const std::size_t path_pos = url.find_first_of("/?#");
    std::string_view authority = url.substr(0, path_pos);
    std::string_view path = path_pos == std::string_view::npos
                                ? std::string_view("/")
                                : url.substr(path_pos);
    if (authority.empty())
    {
        error = "Missing HTTP host";
        return false;
    }
    if (authority.find('@') != std::string_view::npos ||
        authority.front() == '[')
    {
        error = "Unsupported HTTP authority";
        return false;
    }

    const std::size_t colon = authority.rfind(':');
    if (colon != std::string_view::npos)
    {
        if (!parse_port(authority.substr(colon + 1), out.port))
        {
            error = "Invalid HTTP port";
            return false;
        }
        authority = authority.substr(0, colon);
    }
    if (authority.empty())
    {
        error = "Missing HTTP host";
        return false;
    }

    const std::size_t fragment = path.find('#');
    if (fragment != std::string_view::npos)
    {
        path = path.substr(0, fragment);
    }
    if (path.empty())
    {
        path = "/";
    }
    else if (path.front() == '?')
    {
        out.path = "/";
    }

    out.host.assign(authority);
    if (out.path.empty())
    {
        out.path.assign(path);
    }
    else
    {
        out.path.append(path);
    }
    return true;
}

std::string trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }
    return std::string(text);
}

bool ascii_iequals(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i])))
        {
            return false;
        }
    }
    return true;
}

bool ascii_contains(std::string_view text, std::string_view needle)
{
    if (needle.empty() || needle.size() > text.size())
    {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= text.size(); ++i)
    {
        if (ascii_iequals(text.substr(i, needle.size()), needle))
        {
            return true;
        }
    }
    return false;
}

bool parse_response_head(std::string_view text,
                         ResponseHead& out,
                         std::string& error)
{
    const std::size_t first_line_end = text.find("\r\n");
    if (first_line_end == std::string_view::npos)
    {
        error = "Invalid HTTP status line";
        return false;
    }
    const std::string_view status_line = text.substr(0, first_line_end);
    const std::size_t first_space = status_line.find(' ');
    if (!status_line.starts_with("HTTP/") || first_space == std::string_view::npos ||
        first_space + 4 > status_line.size())
    {
        error = "Invalid HTTP status line";
        return false;
    }
    const std::string status_text(status_line.substr(first_space + 1, 3));
    char* status_end = nullptr;
    const long status = std::strtol(status_text.c_str(), &status_end, 10);
    if (!status_end || *status_end != '\0' || status < 100 || status > 999)
    {
        error = "Invalid HTTP status";
        return false;
    }
    out.status = static_cast<int>(status);

    std::size_t offset = first_line_end + 2;
    while (offset < text.size())
    {
        const std::size_t end = text.find("\r\n", offset);
        const std::size_t line_end = end == std::string_view::npos ? text.size() : end;
        const std::string_view line = text.substr(offset, line_end - offset);
        if (line.empty())
        {
            break;
        }
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos)
        {
            const std::string_view name = line.substr(0, colon);
            const std::string value = trim(line.substr(colon + 1));
            if (ascii_iequals(name, "Content-Length"))
            {
                errno = 0;
                char* value_end = nullptr;
                const long long length = std::strtoll(value.c_str(), &value_end, 10);
                if (errno != 0 || !value_end || *value_end != '\0' || length < 0)
                {
                    error = "Invalid HTTP content length";
                    return false;
                }
                out.content_length = length;
            }
            else if (ascii_iequals(name, "Transfer-Encoding"))
            {
                out.chunked = ascii_contains(value, "chunked");
            }
            else if (ascii_iequals(name, "Location"))
            {
                out.location = value;
            }
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        offset = end + 2;
    }
    if (out.chunked)
    {
        out.content_length = -1;
    }
    return true;
}

bool make_redirect_url(const ParsedUrl& base,
                       const std::string& location,
                       std::string& out,
                       std::string& error)
{
    if (location.starts_with("http://") || location.starts_with("https://"))
    {
        out = location;
        return true;
    }
    if (location.empty() || location.front() != '/')
    {
        error = "Unsupported HTTP redirect";
        return false;
    }
    out = base.tls ? "https://" : "http://";
    out += base.host;
    if ((!base.tls && base.port != 80) || (base.tls && base.port != 443))
    {
        out += ':';
        out += std::to_string(base.port);
    }
    out += location;
    return true;
}

class TcpConnection
{
  public:
    explicit TcpConnection(WifiTcpTransport& transport) : transport_(transport) {}

    ~TcpConnection()
    {
        transport_.closeTcp();
    }

    bool open(const ParsedUrl& url, std::int64_t deadline_us, std::string& error)
    {
        transport_.closeTcp();
        if (!transport_.openTcp(url.host.c_str(), url.port))
        {
            const char* detail = transport_.tcpStatus().detail;
            error = detail ? detail : "TCP open failed";
            return false;
        }
        while (before_deadline(deadline_us))
        {
            const auto status = transport_.tcpStatus();
            if (status.state == WifiTcpState::Connected)
            {
                return true;
            }
            if (status.state == WifiTcpState::Error ||
                status.state == WifiTcpState::Disconnected)
            {
                error = status.detail ? status.detail : "TCP open failed";
                return false;
            }
            ::platform::esp::idf_common::wireless_companion::c6_companion().poll();
            poll_delay();
        }
        error = "TCP open timeout";
        return false;
    }

    int read(std::uint8_t* out, std::size_t max_len)
    {
        const std::size_t bytes = transport_.readTcp(out, max_len);
        if (bytes > 0)
        {
            return static_cast<int>(bytes);
        }
        const auto status = transport_.tcpStatus();
        if (status.state == WifiTcpState::Connected ||
            status.state == WifiTcpState::Opening)
        {
            return 0;
        }
        return -1;
    }

    bool write_all(const std::uint8_t* data,
                   std::size_t len,
                   std::int64_t deadline_us,
                   std::string& error)
    {
        if (!before_deadline(deadline_us))
        {
            error = "TCP write timeout";
            return false;
        }
        if (!transport_.writeTcp(data, len))
        {
            const char* detail = transport_.tcpStatus().detail;
            error = detail ? detail : "TCP write failed";
            return false;
        }
        return true;
    }

    WifiTcpTransport& transport()
    {
        return transport_;
    }

  private:
    WifiTcpTransport& transport_;
};

class TlsSession
{
  public:
    TlsSession()
    {
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&config_);
        mbedtls_ctr_drbg_init(&drbg_);
        mbedtls_entropy_init(&entropy_);
    }

    ~TlsSession()
    {
        mbedtls_ssl_free(&ssl_);
        mbedtls_ssl_config_free(&config_);
        mbedtls_ctr_drbg_free(&drbg_);
        mbedtls_entropy_free(&entropy_);
    }

    TlsSession(const TlsSession&) = delete;
    TlsSession& operator=(const TlsSession&) = delete;

    bool begin(TcpConnection& tcp,
               const std::string& hostname,
               std::int64_t deadline_us,
               std::string& error)
    {
        tcp_ = &tcp;
        static constexpr unsigned char kPersonalization[] = "trail-mate-p4-http";
        int result = mbedtls_ctr_drbg_seed(&drbg_,
                                           mbedtls_entropy_func,
                                           &entropy_,
                                           kPersonalization,
                                           sizeof(kPersonalization) - 1);
        if (result == 0)
        {
            result = mbedtls_ssl_config_defaults(&config_,
                                                 MBEDTLS_SSL_IS_CLIENT,
                                                 MBEDTLS_SSL_TRANSPORT_STREAM,
                                                 MBEDTLS_SSL_PRESET_DEFAULT);
        }
        if (result == 0)
        {
            mbedtls_ssl_conf_authmode(&config_, MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_ssl_conf_rng(&config_, mbedtls_ctr_drbg_random, &drbg_);
            result = esp_crt_bundle_attach(&config_) == ESP_OK ? 0 : -1;
        }
        if (result == 0)
        {
            result = mbedtls_ssl_setup(&ssl_, &config_);
        }
        if (result == 0)
        {
            result = mbedtls_ssl_set_hostname(&ssl_, hostname.c_str());
        }
        if (result != 0)
        {
            set_tls_error("TLS setup failed", result, error);
            return false;
        }

        mbedtls_ssl_set_bio(&ssl_, this, &TlsSession::send_callback,
                            &TlsSession::receive_callback, nullptr);
        while (before_deadline(deadline_us))
        {
            result = mbedtls_ssl_handshake(&ssl_);
            if (result == 0)
            {
                return true;
            }
            if (result != MBEDTLS_ERR_SSL_WANT_READ &&
                result != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                set_tls_error("TLS handshake failed", result, error);
                return false;
            }
            poll_delay();
        }
        error = "TLS handshake timeout";
        return false;
    }

    int read(std::uint8_t* out, std::size_t max_len)
    {
        const int result = mbedtls_ssl_read(&ssl_, out, max_len);
        if (result == MBEDTLS_ERR_SSL_WANT_READ ||
            result == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            return 0;
        }
        if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
            result == MBEDTLS_ERR_NET_CONN_RESET)
        {
            return -1;
        }
        return result;
    }

    bool write_all(const std::uint8_t* data,
                   std::size_t len,
                   std::int64_t deadline_us,
                   std::string& error)
    {
        std::size_t offset = 0;
        while (offset < len && before_deadline(deadline_us))
        {
            const int result = mbedtls_ssl_write(&ssl_, data + offset, len - offset);
            if (result > 0)
            {
                offset += static_cast<std::size_t>(result);
                continue;
            }
            if (result != MBEDTLS_ERR_SSL_WANT_READ &&
                result != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                set_tls_error("TLS write failed", result, error);
                return false;
            }
            poll_delay();
        }
        if (offset != len)
        {
            error = "TLS write timeout";
            return false;
        }
        return true;
    }

  private:
    static int send_callback(void* context,
                             const unsigned char* data,
                             std::size_t len)
    {
        auto* self = static_cast<TlsSession*>(context);
        if (!self || !self->tcp_ ||
            !self->tcp_->transport().writeTcp(data, len))
        {
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }
        return static_cast<int>(len);
    }

    static int receive_callback(void* context,
                                unsigned char* out,
                                std::size_t max_len)
    {
        auto* self = static_cast<TlsSession*>(context);
        if (!self || !self->tcp_)
        {
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }
        const int result = self->tcp_->read(out, max_len);
        if (result > 0)
        {
            return result;
        }
        return result == 0 ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_CONN_RESET;
    }

    static void set_tls_error(const char* prefix, int result, std::string& error)
    {
        char detail[96] = {};
        mbedtls_strerror(result, detail, sizeof(detail));
        char message[144] = {};
        std::snprintf(message, sizeof(message), "%s: %s", prefix, detail);
        error = message;
    }

    TcpConnection* tcp_ = nullptr;
    mbedtls_ssl_context ssl_{};
    mbedtls_ssl_config config_{};
    mbedtls_ctr_drbg_context drbg_{};
    mbedtls_entropy_context entropy_{};
};

class Stream
{
  public:
    bool begin(const ParsedUrl& url,
               std::int64_t deadline_us,
               std::string& error)
    {
        tcp_ = std::unique_ptr<TcpConnection>(new (std::nothrow)
                                                  TcpConnection(::platform::esp::idf_common::
                                                                    wireless_companion::
                                                                        c6_wifi_tcp_transport()));
        if (!tcp_)
        {
            error = "Allocate TCP connection failed";
            return false;
        }
        if (!tcp_->open(url, deadline_us, error))
        {
            return false;
        }
        if (!url.tls)
        {
            return true;
        }
        tls_ = std::unique_ptr<TlsSession>(new (std::nothrow) TlsSession());
        if (!tls_)
        {
            error = "Allocate TLS session failed";
            return false;
        }
        return tls_->begin(*tcp_, url.host, deadline_us, error);
    }

    int read(std::uint8_t* out, std::size_t max_len)
    {
        return tls_ ? tls_->read(out, max_len) : tcp_->read(out, max_len);
    }

    bool write_all(const std::uint8_t* data,
                   std::size_t len,
                   std::int64_t deadline_us,
                   std::string& error)
    {
        return tls_ ? tls_->write_all(data, len, deadline_us, error)
                    : tcp_->write_all(data, len, deadline_us, error);
    }

  private:
    std::unique_ptr<TcpConnection> tcp_;
    std::unique_ptr<TlsSession> tls_;
};

class ChunkedDecoder
{
  public:
    bool feed(const std::uint8_t* data,
              std::size_t len,
              WriteCallback write,
              void* context,
              std::uint32_t max_bytes,
              std::uint32_t& bytes,
              std::string& error)
    {
        std::size_t offset = 0;
        while (offset < len && state_ != State::Done)
        {
            if (state_ == State::Data)
            {
                const std::size_t take = std::min<std::size_t>(remaining_, len - offset);
                if (max_bytes > 0 &&
                    static_cast<std::uint64_t>(bytes) + take > max_bytes)
                {
                    error = "HTTP response too large";
                    return false;
                }
                if (take > 0 && !write(data + offset, take, context))
                {
                    error = "Write HTTP response failed";
                    return false;
                }
                bytes += static_cast<std::uint32_t>(take);
                remaining_ -= take;
                offset += take;
                if (remaining_ == 0)
                {
                    state_ = State::DataCr;
                }
                continue;
            }

            const char c = static_cast<char>(data[offset++]);
            if (state_ == State::DataCr)
            {
                if (c != '\r')
                {
                    error = "Invalid HTTP chunk terminator";
                    return false;
                }
                state_ = State::DataLf;
            }
            else if (state_ == State::DataLf)
            {
                if (c != '\n')
                {
                    error = "Invalid HTTP chunk terminator";
                    return false;
                }
                state_ = State::Size;
            }
            else if (c == '\r')
            {
                saw_cr_ = true;
            }
            else if (c == '\n' && saw_cr_)
            {
                saw_cr_ = false;
                if (state_ == State::Size)
                {
                    if (!parse_size(error))
                    {
                        return false;
                    }
                }
                else if (state_ == State::Trailer)
                {
                    if (line_.empty())
                    {
                        state_ = State::Done;
                    }
                    line_.clear();
                }
            }
            else
            {
                if (saw_cr_)
                {
                    line_.push_back('\r');
                    saw_cr_ = false;
                }
                line_.push_back(c);
                if (line_.size() > kMaxChunkLineBytes)
                {
                    error = "HTTP chunk line too long";
                    return false;
                }
            }
        }
        return true;
    }

    bool done() const
    {
        return state_ == State::Done;
    }

  private:
    enum class State : std::uint8_t
    {
        Size,
        Data,
        DataCr,
        DataLf,
        Trailer,
        Done,
    };

    bool parse_size(std::string& error)
    {
        const std::size_t extension = line_.find(';');
        const std::string token = trim(std::string_view(line_).substr(0, extension));
        if (token.empty())
        {
            error = "Invalid HTTP chunk size";
            return false;
        }
        errno = 0;
        char* end = nullptr;
        const unsigned long long value = std::strtoull(token.c_str(), &end, 16);
        if (errno != 0 || !end || *end != '\0' || value > SIZE_MAX)
        {
            error = "Invalid HTTP chunk size";
            return false;
        }
        line_.clear();
        remaining_ = static_cast<std::size_t>(value);
        state_ = remaining_ == 0 ? State::Trailer : State::Data;
        return true;
    }

    State state_ = State::Size;
    std::string line_;
    std::size_t remaining_ = 0;
    bool saw_cr_ = false;
};

void set_stats(TransferStats* stats, int status, std::uint32_t bytes)
{
    if (stats)
    {
        stats->http_status = status;
        stats->bytes = bytes;
    }
}

bool deliver_body(const std::uint8_t* data,
                  std::size_t len,
                  const Request& request,
                  WriteCallback write,
                  void* context,
                  std::uint32_t total_bytes,
                  std::uint32_t& bytes,
                  std::string& error)
{
    if (request.max_bytes > 0 &&
        static_cast<std::uint64_t>(bytes) + len > request.max_bytes)
    {
        error = "HTTP response too large";
        return false;
    }
    if (len > 0 && !write(data, len, context))
    {
        error = "Write HTTP response failed";
        return false;
    }
    bytes += static_cast<std::uint32_t>(len);
    if (request.progress)
    {
        request.progress(bytes, total_bytes, request.progress_context);
    }
    return true;
}

bool transfer_once(const std::string& url_text,
                   const Request& request,
                   const wifi_access::Lease& lease,
                   WriteCallback write,
                   void* write_context,
                   int& out_status,
                   std::uint32_t& out_bytes,
                   std::string& out_redirect,
                   std::string& out_error)
{
    ParsedUrl url;
    if (!parse_url(url_text, url, out_error))
    {
        return false;
    }
    const std::int64_t deadline_us = deadline_after_ms(request.timeout_ms);
    Stream stream;
    if (!stream.begin(url, deadline_us, out_error))
    {
        return false;
    }

    std::string request_text = "GET ";
    request_text += url.path;
    request_text += " HTTP/1.1\r\nHost: ";
    request_text += url.host;
    if ((!url.tls && url.port != 80) || (url.tls && url.port != 443))
    {
        request_text += ':';
        request_text += std::to_string(url.port);
    }
    request_text +=
        "\r\nUser-Agent: Trail-Mate-P4/1\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    if (!stream.write_all(reinterpret_cast<const std::uint8_t*>(request_text.data()),
                          request_text.size(),
                          deadline_us,
                          out_error))
    {
        return false;
    }

    const std::size_t buffer_size = request.buffer_size > 0
                                        ? static_cast<std::size_t>(request.buffer_size)
                                        : kDefaultBufferBytes;
    HeapBuffer buffer;
    if (!buffer.allocate(buffer_size, request_prefers_psram(request)))
    {
        out_error = "Allocate HTTP buffer failed";
        return false;
    }

    std::string head_and_prefix;
    head_and_prefix.reserve(std::min<std::size_t>(kMaxResponseHeaderBytes,
                                                  buffer_size * 2U));
    std::size_t head_end = std::string::npos;
    while (before_deadline(deadline_us) && head_end == std::string::npos)
    {
        const int read = stream.read(buffer.data(), buffer.size());
        if (read < 0)
        {
            out_error = "HTTP connection closed before headers";
            return false;
        }
        if (read == 0)
        {
            poll_delay();
            continue;
        }
        head_and_prefix.append(reinterpret_cast<const char*>(buffer.data()),
                               static_cast<std::size_t>(read));
        head_end = head_and_prefix.find("\r\n\r\n");
        if (head_end == std::string::npos &&
            head_and_prefix.size() > kMaxResponseHeaderBytes)
        {
            out_error = "HTTP response headers too large";
            return false;
        }
    }
    if (head_end == std::string::npos)
    {
        out_error = "HTTP response header timeout";
        return false;
    }

    ResponseHead response;
    if (!parse_response_head(std::string_view(head_and_prefix).substr(0, head_end + 2),
                             response,
                             out_error))
    {
        return false;
    }
    out_status = response.status;
    if (response.status >= 300 && response.status < 400)
    {
        if (response.location.empty())
        {
            out_error = "HTTP redirect missing location";
            return false;
        }
        return make_redirect_url(url, response.location, out_redirect, out_error);
    }
    if (response.status < 200 || response.status >= 300)
    {
        char error[48] = {};
        std::snprintf(error, sizeof(error), "HTTP %d", response.status);
        out_error = error;
        return false;
    }
    if (response.content_length > 0 && request.max_bytes > 0 &&
        static_cast<std::uint64_t>(response.content_length) > request.max_bytes)
    {
        out_error = "HTTP response too large";
        return false;
    }
    if (response.content_length > UINT32_MAX)
    {
        out_error = "HTTP response too large";
        return false;
    }

    const std::uint32_t total_bytes =
        response.content_length > 0 &&
                static_cast<std::uint64_t>(response.content_length) <= UINT32_MAX
            ? static_cast<std::uint32_t>(response.content_length)
            : 0;
    if (request.progress)
    {
        request.progress(0, total_bytes, request.progress_context);
    }

    const std::size_t body_offset = head_end + 4;
    const auto* prefix = reinterpret_cast<const std::uint8_t*>(head_and_prefix.data() + body_offset);
    std::size_t prefix_len = head_and_prefix.size() - body_offset;
    ChunkedDecoder chunked;
    if (response.chunked)
    {
        if (!chunked.feed(prefix,
                          prefix_len,
                          write,
                          write_context,
                          request.max_bytes,
                          out_bytes,
                          out_error))
        {
            return false;
        }
        if (request.progress)
        {
            request.progress(out_bytes, 0, request.progress_context);
        }
    }
    else
    {
        if (response.content_length >= 0)
        {
            const std::uint64_t remaining =
                static_cast<std::uint64_t>(response.content_length);
            prefix_len = static_cast<std::size_t>(
                std::min<std::uint64_t>(prefix_len, remaining));
        }
        if (!deliver_body(prefix,
                          prefix_len,
                          request,
                          write,
                          write_context,
                          total_bytes,
                          out_bytes,
                          out_error))
        {
            return false;
        }
    }

    while (before_deadline(deadline_us))
    {
        if (wifi_access::lease_revoked(lease))
        {
            out_error = wifi_access::decision_name(wifi_access::Decision::CallExclusive);
            return false;
        }
        if (response.chunked && chunked.done())
        {
            return true;
        }
        if (!response.chunked && response.content_length >= 0 &&
            out_bytes >= static_cast<std::uint64_t>(response.content_length))
        {
            return true;
        }

        const int read = stream.read(buffer.data(), buffer.size());
        if (read < 0)
        {
            if (response.chunked)
            {
                out_error = "HTTP chunked response ended early";
                return false;
            }
            if (response.content_length >= 0 &&
                out_bytes != static_cast<std::uint64_t>(response.content_length))
            {
                out_error = "HTTP response ended early";
                return false;
            }
            return true;
        }
        if (read == 0)
        {
            poll_delay();
            continue;
        }

        std::size_t len = static_cast<std::size_t>(read);
        if (response.chunked)
        {
            if (!chunked.feed(buffer.data(),
                              len,
                              write,
                              write_context,
                              request.max_bytes,
                              out_bytes,
                              out_error))
            {
                return false;
            }
            if (request.progress)
            {
                request.progress(out_bytes, 0, request.progress_context);
            }
        }
        else
        {
            if (response.content_length >= 0)
            {
                const std::uint64_t remaining =
                    static_cast<std::uint64_t>(response.content_length) - out_bytes;
                len = static_cast<std::size_t>(
                    std::min<std::uint64_t>(len, remaining));
            }
            if (!deliver_body(buffer.data(),
                              len,
                              request,
                              write,
                              write_context,
                              total_bytes,
                              out_bytes,
                              out_error))
            {
                return false;
            }
        }
    }
    out_error = "HTTP response timeout";
    return false;
}

} // namespace

bool get_text(const Request& request,
              std::string& out,
              std::string& out_error,
              TransferStats* out_stats)
{
    out.clear();
    return download(
        request,
        [](const std::uint8_t* data, std::size_t len, void* context)
        {
            static_cast<std::string*>(context)->append(
                reinterpret_cast<const char*>(data), len);
            return true;
        },
        &out,
        out_error,
        out_stats);
}

bool download(const Request& request,
              WriteCallback write,
              void* write_context,
              std::string& out_error,
              TransferStats* out_stats)
{
    std::lock_guard<std::mutex> lock(s_http_mutex);
    out_error.clear();
    set_stats(out_stats, 0, 0);
    if (!write)
    {
        out_error = "Missing HTTP writer";
        return false;
    }
    if (!validate_request(request, out_error))
    {
        return false;
    }

    wifi_access::Request access_request{};
    access_request.client = request.client;
    access_request.kind = request.access_kind;
    access_request.priority = request.priority;
    access_request.allow_connect = true;
    access_request.reason = request.reason;
    const wifi_access::Lease lease = wifi_access::acquire(access_request);
    if (!lease.granted)
    {
        out_error = wifi_access::decision_name(lease.decision);
        return false;
    }

    bool ok = false;
    wifi_access::ConnectResult connect_result{};
    if (!wifi_access::ensure_connected(access_request, &connect_result))
    {
        out_error = wifi_access::decision_name(connect_result.decision);
    }
    else if (!memory_ready(request))
    {
        out_error = "Low HTTP memory";
    }
    else
    {
        std::string current_url = request.url;
        for (unsigned redirects = 0; redirects <= kMaxRedirects; ++redirects)
        {
            if (wifi_access::lease_revoked(lease))
            {
                out_error = wifi_access::decision_name(wifi_access::Decision::CallExclusive);
                break;
            }
            int status = 0;
            std::uint32_t bytes = 0;
            std::string redirect;
            ok = transfer_once(current_url,
                               request,
                               lease,
                               write,
                               write_context,
                               status,
                               bytes,
                               redirect,
                               out_error);
            set_stats(out_stats, status, bytes);
            if (!ok || redirect.empty())
            {
                break;
            }
            if (redirects == kMaxRedirects)
            {
                ok = false;
                out_error = "Too many HTTP redirects";
                break;
            }
            current_url = std::move(redirect);
        }
    }

    wifi_access::release(lease);
    if (!ok)
    {
        ESP_LOGW(kTag, "HTTP request failed: %s", out_error.c_str());
    }
    return ok;
}

} // namespace platform::ui::http_client

#else

namespace platform::ui::http_client
{

bool get_text(const Request&,
              std::string& out,
              std::string& out_error,
              TransferStats* out_stats)
{
    out.clear();
    out_error = "HTTP unavailable on this IDF target";
    if (out_stats)
    {
        *out_stats = {};
    }
    return false;
}

bool download(const Request&,
              WriteCallback,
              void*,
              std::string& out_error,
              TransferStats* out_stats)
{
    out_error = "HTTP unavailable on this IDF target";
    if (out_stats)
    {
        *out_stats = {};
    }
    return false;
}

} // namespace platform::ui::http_client

#endif
