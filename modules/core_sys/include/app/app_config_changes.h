#pragma once

#include <cstdint>

namespace app
{

enum class AppConfigChangeDomain : std::uint32_t
{
    Identity = 1UL << 0,
    Mesh = 1UL << 1,
    Channels = 1UL << 2,
    Gps = 1UL << 3,
    Map = 1UL << 4,
    ChatUi = 1UL << 5,
    Network = 1UL << 6,
    Privacy = 1UL << 7,
    Route = 1UL << 8,
    Aprs = 1UL << 9,
};

class AppConfigChangeSet
{
  public:
    using Bits = std::uint32_t;

    constexpr AppConfigChangeSet() = default;
    constexpr explicit AppConfigChangeSet(Bits bits) : bits_(bits) {}
    constexpr explicit AppConfigChangeSet(AppConfigChangeDomain domain)
        : bits_(static_cast<Bits>(domain))
    {
    }

    constexpr Bits bits() const
    {
        return bits_;
    }

    constexpr bool empty() const
    {
        return bits_ == 0;
    }

    constexpr bool contains(AppConfigChangeDomain domain) const
    {
        return (bits_ & static_cast<Bits>(domain)) != 0;
    }

    constexpr bool intersects(AppConfigChangeSet other) const
    {
        return (bits_ & other.bits_) != 0;
    }

    constexpr AppConfigChangeSet merged(AppConfigChangeSet other) const
    {
        return AppConfigChangeSet(bits_ | other.bits_);
    }

    void mergeIn(AppConfigChangeSet other)
    {
        bits_ |= other.bits_;
    }

    void add(AppConfigChangeDomain domain)
    {
        bits_ |= static_cast<Bits>(domain);
    }

    static constexpr AppConfigChangeSet none()
    {
        return AppConfigChangeSet();
    }

    static constexpr AppConfigChangeSet identity()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Identity);
    }

    static constexpr AppConfigChangeSet mesh()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Mesh);
    }

    static constexpr AppConfigChangeSet channels()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Channels);
    }

    static constexpr AppConfigChangeSet gps()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Gps);
    }

    static constexpr AppConfigChangeSet map()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Map);
    }

    static constexpr AppConfigChangeSet chatUi()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::ChatUi);
    }

    static constexpr AppConfigChangeSet network()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Network);
    }

    static constexpr AppConfigChangeSet privacy()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Privacy);
    }

    static constexpr AppConfigChangeSet route()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Route);
    }

    static constexpr AppConfigChangeSet aprs()
    {
        return AppConfigChangeSet(AppConfigChangeDomain::Aprs);
    }

    static constexpr AppConfigChangeSet allPersisted()
    {
        return AppConfigChangeSet(static_cast<Bits>(AppConfigChangeDomain::Identity) |
                                  static_cast<Bits>(AppConfigChangeDomain::Mesh) |
                                  static_cast<Bits>(AppConfigChangeDomain::Channels) |
                                  static_cast<Bits>(AppConfigChangeDomain::Gps) |
                                  static_cast<Bits>(AppConfigChangeDomain::Map) |
                                  static_cast<Bits>(AppConfigChangeDomain::ChatUi) |
                                  static_cast<Bits>(AppConfigChangeDomain::Network) |
                                  static_cast<Bits>(AppConfigChangeDomain::Privacy) |
                                  static_cast<Bits>(AppConfigChangeDomain::Route) |
                                  static_cast<Bits>(AppConfigChangeDomain::Aprs));
    }

  private:
    Bits bits_ = 0;
};

inline constexpr AppConfigChangeSet operator|(AppConfigChangeSet lhs,
                                              AppConfigChangeSet rhs)
{
    return lhs.merged(rhs);
}

} // namespace app
