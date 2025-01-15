#include "Paths.h"

#include "Companion.h"
#include "utils/Decompressor.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) "0x" << std::hex << std::setw(2) << std::setfill('0') << c

ExportResult MK64::WaypointHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                  std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern TrackPathPoint " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult MK64::WaypointCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto pathPoint = std::static_pointer_cast<WaypointData>(raw)->mPaths;
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    write << "TrackPathPoint " << symbol << "[] = {\n";

    for (int i = 0; i < pathPoint.size(); ++i) {
        auto x = pathPoint[i].posX;
        auto y = pathPoint[i].posY;
        auto z = pathPoint[i].posZ;

        // Track segment
        auto seg = pathPoint[i].trackSegment;

        if (i <= pathPoint.size() - 1) {
            write << fourSpaceTab;
        }

        // {{{ x, y, z }, f, { tc1, tc2 }, { c1, c2, c3, c4 }}}
        write << "{" << NUM(x) << ", " << NUM(y) << ", " << NUM(z) << ", " << NUM(seg) << " },\n";
    }
    write << "};\n";

    return offset + pathPoint.size() * sizeof(MK64::TrackPathPoint);
}

ExportResult MK64::WaypointBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                  std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto pathPoint = std::static_pointer_cast<WaypointData>(raw)->mPaths;
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::Paths, 0);
    writer.Write((uint32_t) pathPoint.size());
    for (auto w : pathPoint) {
        writer.Write(w.posX);
        writer.Write(w.posY);
        writer.Write(w.posZ);
        writer.Write(w.trackSegment);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> MK64::PathsFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto count = GetSafeNode<size_t>(node, "count");

    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, count * sizeof(MK64::TrackPathPoint));

    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<MK64::TrackPathPoint> pathPoint;

    for (size_t i = 0; i < count; i++) {
        auto x = reader.ReadInt16();
        auto y = reader.ReadInt16();
        auto z = reader.ReadInt16();
        auto trackSegment = reader.ReadUInt16();

        pathPoint.push_back(MK64::TrackPathPoint({ x, y, z, trackSegment }));
    }

    return std::make_shared<WaypointData>(pathPoint);
}