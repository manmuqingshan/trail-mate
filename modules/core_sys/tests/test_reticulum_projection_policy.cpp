#include "platform/ui/reticulum_contact_projection_policy.h"
#include "platform/ui/reticulum_network_projection_policy.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace
{

namespace rtdir = ::platform::ui::reticulum_directory;
namespace rtcontacts = ::platform::ui::reticulum_contacts;
namespace rtnetwork = ::platform::ui::reticulum_network;

void fill(uint8_t* data, std::size_t len, uint8_t value)
{
    for (std::size_t index = 0; index < len; ++index)
    {
        data[index] = static_cast<uint8_t>(value + index);
    }
}

rtdir::LxmfAddressRecord valid_address()
{
    rtdir::LxmfAddressRecord record{};
    record.valid = true;
    fill(record.destination_hash, sizeof(record.destination_hash), 1);
    fill(record.identity_hash, sizeof(record.identity_hash), 17);
    fill(record.enc_pub, sizeof(record.enc_pub), 33);
    fill(record.sig_pub, sizeof(record.sig_pub), 65);
    record.source = rtdir::EntrySource::RuntimeRx;
    return record;
}

rtdir::AnnounceRecord announce(rtdir::AnnounceAspect aspect)
{
    rtdir::AnnounceRecord record{};
    record.valid = true;
    record.aspect = aspect;
    return record;
}

void contacts_policy_keeps_contacts_person_shaped()
{
    auto runtime = valid_address();
    assert(rtcontacts::classify(runtime) ==
           rtcontacts::ProjectionBucket::Announced);

    auto favorite = runtime;
    favorite.favorite = true;
    assert(rtcontacts::classify(favorite) ==
           rtcontacts::ProjectionBucket::Contact);

    auto manual = runtime;
    manual.source = rtdir::EntrySource::Manual;
    assert(rtcontacts::classify(manual) ==
           rtcontacts::ProjectionBucket::Contact);

    auto ignored = runtime;
    ignored.ignored = true;
    assert(rtcontacts::classify(ignored) ==
           rtcontacts::ProjectionBucket::Ignored);

    auto invalid = runtime;
    invalid.valid = false;
    assert(rtcontacts::classify(invalid) ==
           rtcontacts::ProjectionBucket::Hidden);
}

void network_policy_keeps_services_out_of_contacts()
{
    assert(rtnetwork::classify(announce(rtdir::AnnounceAspect::LxmfDelivery)) ==
           rtnetwork::ProjectionBucket::Hidden);
    assert(rtnetwork::classify(announce(rtdir::AnnounceAspect::LxmfPropagation)) ==
           rtnetwork::ProjectionBucket::MessageRelay);
    assert(rtnetwork::classify(announce(rtdir::AnnounceAspect::NomadNetworkNode)) ==
           rtnetwork::ProjectionBucket::WebOrService);
    assert(rtnetwork::classify(announce(rtdir::AnnounceAspect::CallAudio)) ==
           rtnetwork::ProjectionBucket::TelephonyService);
    assert(rtnetwork::classify(announce(rtdir::AnnounceAspect::Unknown)) ==
           rtnetwork::ProjectionBucket::UnknownService);

    auto invalid = announce(rtdir::AnnounceAspect::NomadNetworkNode);
    invalid.valid = false;
    assert(rtnetwork::classify(invalid) ==
           rtnetwork::ProjectionBucket::Hidden);
}

} // namespace

int main()
{
    contacts_policy_keeps_contacts_person_shaped();
    network_policy_keeps_services_out_of_contacts();
    return 0;
}
