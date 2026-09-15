#include "platform/esp/arduino_common/map_poi/cjson_poi_parser.h"
#include <cassert>
#include <cstring>
#include <string>

using platform::esp::arduino_common::map_poi::CJsonPoiParser;
int main()
{
    CJsonPoiParser parser;
    ui::map_poi::Policy policy;
    const std::string manifest = R"({"version":3,"annotation_options":{"RoadZooms":{"Minimum":7,"Maximum":18}},"index":{"enabled_zoom_levels":[1,16,17,18],"scheme":"web-mercator-xyz","format":"jsonl","geometry":"tile-local-pixels","max_path_points":8,"tile_margin":32}})";
    assert(parser.manifest(manifest.data(), manifest.size(), policy));
    assert(policy.schema_version == 3 && policy.enabled(1) && policy.enabled(18) && !policy.enabled(15));
    auto incompatible = manifest;
    incompatible.replace(incompatible.find("tile-local-pixels"), 17, "geographic-degrees");
    assert(!parser.manifest(incompatible.data(), incompatible.size(), policy));

    ui::map_poi::Record record;
    const std::string road = u8R"({"id":"ABCDEF0123456789","feature_id":"0123456789ABCDEF","kind":"road","type":"residential","name":"人民路","lat":25.04,"lon":102.71,"priority":175,"path":[-32,80,100,80,288,90]})";
    assert(parser.record(road.data(), road.size(), record));
    assert(record.explicit_kind && record.kind == ui::map::AnnotationKind::Road);
    assert(record.feature_key == UINT64_C(0x0123456789ABCDEF));
    assert(record.path_points == 3 && record.path[0] == -32 && record.path[4] == 288);
    assert(std::strcmp(record.name, u8"人民路") == 0);
    auto wrong_path = road;
    wrong_path.replace(wrong_path.find("288"), 3, "289");
    assert(!parser.record(wrong_path.data(), wrong_path.size(), record));
    const std::string place = u8R"({"id":"1111111111111111","feature_id":"2222222222222222","kind":"place","type":"regional_capital","name":"昆明市","lat":25.04,"lon":102.71,"priority":225})";
    assert(parser.record(place.data(), place.size(), record));
    assert(record.kind == ui::map::AnnotationKind::Place && record.path_points == 0);
    const std::string old = R"({"id":"node/1","type":"water","name":"Water","lat":25,"lon":102,"priority":90})";
    assert(parser.record(old.data(), old.size(), record));
    assert(!record.explicit_kind && record.kind == ui::map::AnnotationKind::Poi && record.feature_key != 0);
    const std::string unnamed_road = R"({"id":"a","feature_id":"2222222222222222","kind":"road","type":"service","lat":25,"lon":102,"path":[0,0,256,256]})";
    assert(!parser.record(unnamed_road.data(), unnamed_road.size(), record));
}
