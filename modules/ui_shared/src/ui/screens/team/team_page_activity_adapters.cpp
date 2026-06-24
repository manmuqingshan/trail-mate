#include "ui/screens/team/team_page_activity_sink.h"

#include "platform/ui/team_ui_store_runtime.h"
#include "sys/event_bus.h"
#include "ui/screens/gps/gps_page_runtime.h"

namespace team
{
namespace ui
{

bool TeamPageActivityStoreAdapter::appendPosition(const TeamId& team_id,
                                                  uint32_t member_id,
                                                  int32_t lat_e7,
                                                  int32_t lon_e7,
                                                  int16_t alt_m,
                                                  uint16_t speed_dmps,
                                                  uint32_t ts)
{
    return team_ui_posring_append(team_id,
                                  member_id,
                                  lat_e7,
                                  lon_e7,
                                  alt_m,
                                  speed_dmps,
                                  ts);
}

bool TeamPageActivityStoreAdapter::appendMemberTrack(
    const TeamId& team_id,
    uint32_t member_id,
    const team::proto::TeamTrackMessage& track)
{
    return team_ui_append_member_track(team_id, member_id, track);
}

bool TeamPageActivityStoreAdapter::memberTrackPath(const TeamId& team_id,
                                                   uint32_t member_id,
                                                   std::string& out_path)
{
    return team_ui_get_member_track_path(team_id, member_id, out_path);
}

bool TeamPageActivityStoreAdapter::appendStructuredChat(
    const TeamId& team_id,
    uint32_t peer_id,
    bool incoming,
    uint32_t ts,
    team::proto::TeamChatType type,
    const std::vector<uint8_t>& payload)
{
    return team_ui_chatlog_append_structured(team_id,
                                             peer_id,
                                             incoming,
                                             ts,
                                             type,
                                             payload);
}

uint32_t TeamPageGpsTrackLoaderAdapter::selectedMemberId() const
{
    return gps::ui::runtime::selected_map_member_id();
}

bool TeamPageGpsTrackLoaderAdapter::loadTrackFile(const char* path,
                                                  bool show_toast)
{
    return gps::ui::runtime::load_map_track_file(path, show_toast);
}

void TeamPageUnreadPublisherAdapter::publishTeamUnread(uint32_t unread_count)
{
    sys::EventBus::publish(new sys::ChatUnreadChangedEvent(
                               2,
                               static_cast<int>(unread_count)),
                           0);
}

} // namespace ui
} // namespace team
