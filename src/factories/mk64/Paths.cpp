#include "Paths.h"

#include "Companion.h"
#include "utils/Decompressor.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) "0x" << std::hex << std::setw(2) << std::setfill('0') << c

ExportResult MK64::PathHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern TrackPath " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult MK64::PathCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto paths = std::static_pointer_cast<PathData>(raw)->mPaths;
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    write << "TrackPath " << symbol << "[] = {\n";

    for (int i = 0; i < paths.size(); ++i) {
        auto x = paths[i].posX;
        auto y = paths[i].posY;
        auto z = paths[i].posZ;

        // Track segment
        auto seg = paths[i].trackSegment;

        if (i <= paths.size() - 1) {
            write << fourSpaceTab;
        }

        // {{{ x, y, z }, f, { tc1, tc2 }, { c1, c2, c3, c4 }}}
        write << "{" << NUM(x) << ", " << NUM(y) << ", " << NUM(z) << ", " << NUM(seg) << " },\n";
    }
    write << "};\n";

    return offset + paths.size() * sizeof(MK64::TrackPath);
}

ExportResult MK64::PathBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto paths = std::static_pointer_cast<PathData>(raw)->mPaths;
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, Torch::ResourceType::Paths, 0);
    writer.Write((uint32_t) paths.size());
    for (auto w : paths) {
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
    LUS::BinaryReader reader(segment.data, count * sizeof(MK64::TrackPath));

    reader.SetEndianness(Torch::Endianness::Big);
    std::vector<MK64::TrackPath> paths;

    for (size_t i = 0; i < count; i++) {
        auto x = reader.ReadInt16();
        auto y = reader.ReadInt16();
        auto z = reader.ReadInt16();
        auto trackSegment = reader.ReadUInt16();

        paths.push_back(MK64::TrackPath({ x, y, z, trackSegment }));
    }

    return std::make_shared<PathData>(paths);
}