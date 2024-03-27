#include "LightsFactory.h"

#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

ExportResult LightsHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Lights1 " << symbol << ";\n";
    return std::nullopt;
}

ExportResult LightsCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto light = std::static_pointer_cast<LightsData>(raw)->mLights;
    auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "Lights1 " << symbol << " = gdSPDefLights1(\n";

    // Ambient
    auto r = (int16_t) light.a.l.col[0];
    auto g = (int16_t) light.a.l.col[1];
    auto b = (int16_t) light.a.l.col[2];

    // Diffuse
    auto r2 = (int16_t) light.l[0].l.col[0];
    auto g2 = (int16_t) light.l[0].l.col[1];
    auto b2 = (int16_t) light.l[0].l.col[2];

    // Direction
    auto x = (int16_t) light.l[0].l.dir[0];
    auto y = (int16_t) light.l[0].l.dir[1];
    auto z = (int16_t) light.l[0].l.dir[2];

    SPDLOG_INFO("Read light: {:X} {:X} {:X} {:X} {:X}", r, g, b, r2, g2);

    write << fourSpaceTab;
    write << r << ", " << g << ", " << b << ",\n";
    write << fourSpaceTab;
    write << r2 << ", " << g2 << ", " << b2 << ", " << x << ", " << y << ", " << z << "\n";

    write << ");\n\n";
    return std::nullopt;
}

ExportResult LightsBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto light = std::static_pointer_cast<LightsData>(raw)->mLights;
    auto writer = LUS::BinaryWriter();
    auto size = sizeof(light.l) / sizeof(LightRaw);

    WriteHeader(writer, LUS::ResourceType::Lights, 0);

    writer.Write(reinterpret_cast<char*>(light.a.l.col), 3);
    writer.Write(reinterpret_cast<char*>(light.a.l.colc), 3);

    writer.Write(static_cast<uint32_t>(size));

    for(size_t i = 0; i < size; i++) {
        writer.Write(reinterpret_cast<char*>(light.l[i].l.col), 3);
        writer.Write(reinterpret_cast<char*>(light.l[i].l.colc), 3);
        writer.Write(reinterpret_cast<char*>(light.l[i].l.dir), 3);
    }
    writer.Finish(write);
    writer.Close();
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> LightsFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto decoded = Decompressor::AutoDecode(node, buffer);
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, sizeof(Lights1Raw));

    reader.SetEndianness(LUS::Endianness::Big);
    Lights1Raw lights;
    // Directional light

    // Ambient
    auto r = (uint8_t)reader.ReadInt8();
    auto g = (uint8_t)reader.ReadInt8();
    auto b = (uint8_t)reader.ReadInt8();
    //auto nil = reader.ReadInt32();
    reader.Seek(5, LUS::SeekOffsetType::Current);

    // Diffuse copy
    auto r2 = reader.ReadUByte();
    auto g2 = reader.ReadUByte();
    auto b2 = reader.ReadUByte();

    reader.Seek(5, LUS::SeekOffsetType::Current);

    // Direction
    auto x = reader.ReadInt8();
    auto y = reader.ReadInt8();
    auto z = reader.ReadInt8();

    // TODO: This is a hack, but it works for now.
    // The struct has several copies/padding. The zeros skip those for gdSPDefLights1.
    lights = {r, g, b, 0, 0, 0, 0, 0, r2, g2, b2, 0, 0, 0, 0, 0, x, y, z};

    return std::make_shared<LightsData>(lights);
}
