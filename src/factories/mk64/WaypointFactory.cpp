#include "WaypointFactory.h"

#include "Companion.h"
#include "utils/Decompressor.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) "0x" << std::hex << std::setw(2) << std::setfill('0') << c

void MK64::WaypointHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern TrackWaypoint " << symbol << "[];\n";
}

void MK64::WaypointCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto waypoints = std::static_pointer_cast<WaypointData>(raw)->mWaypoints;
    auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "TrackWaypoint " << symbol << "[] = {\n";

    for (int i = 0; i < waypoints.size(); ++i) {
        auto x = waypoints[i].posX;
        auto y = waypoints[i].posY;
        auto z = waypoints[i].posZ;

        // Track segment
        auto seg = waypoints[i].trackSegment;

        if(i <= waypoints.size() - 1) {
            write << fourSpaceTab;
        }

        // {{{ x, y, z }, f, { tc1, tc2 }, { c1, c2, c3, c4 }}}
        write << "{" << NUM(x) << ", " << NUM(y) << ", " << NUM(z) << ", " << NUM(seg) << " },\n";
    }
    write << "};\n\n";
}

void MK64::WaypointBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto waypoints = std::static_pointer_cast<WaypointData>(raw)->mWaypoints;
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::Waypoints, 0);
    writer.Write((uint32_t) waypoints.size());
    for(auto w : waypoints) {
        writer.Write(w.posX);
        writer.Write(w.posY);
        writer.Write(w.posZ);
        writer.Write(w.trackSegment);
    }

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> MK64::WaypointFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto count = GetSafeNode<size_t>(node, "count");

    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, count * sizeof(MK64::TrackWaypoint));

    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<MK64::TrackWaypoint> waypoints;

    for(size_t i = 0; i < count; i++) {
        auto x = reader.ReadInt16();
        auto y = reader.ReadInt16();
        auto z = reader.ReadInt16();
        auto trackSegment = reader.ReadUInt16();

        waypoints.push_back(MK64::TrackWaypoint({
           x, y, z, trackSegment
       }));
    }

    return std::make_shared<WaypointData>(waypoints);
}