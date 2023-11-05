#include "LightsFactory.h"
#include "utils/MIODecoder.h"
#include <iomanip>

void LightsCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto light = std::static_pointer_cast<LightsData>(raw)->mLights;
    auto symbol = node["symbol"].as<std::string>();

    write << "Lights1 " << symbol << "[] = gdSPDefLights1(\n";

    // Diffuse
    auto r = light.l[0].l.col[0];
    auto g = light.l[0].l.col[1];
    auto b = light.l[0].l.col[2];

    // Diffuse copy
    auto r2 = light.l[0].l.colc[0];
    auto g2 = light.l[0].l.colc[1];
    auto b2 = light.l[0].l.colc[2];

    // Direction
    auto x = light.l[0].l.dir[0];
    auto y = light.l[0].l.dir[1];
    auto z = light.l[0].l.dir[2];
    printf("HERE111");
    write << fourSpaceTab;
    write << r << ", " << g << ", " << b << ",\n";
    write << fourSpaceTab;
    write << r2 << ", " << g2 << ", " << b2 << ", " << x << ", " << y << ", " << z << "\n";
    printf("DONE");
    write << ");\n";
}

void LightsBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto light = std::static_pointer_cast<LightsData>(raw)->mLights;
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::Lights, 0);
    writer.Write((uint32_t) sizeof(light));

}

std::optional<std::shared_ptr<IParsedData>> LightsFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto mio0 = node["mio0"].as<size_t>();
    auto offset = node["offset"].as<uint32_t>();
    auto count = node["count"].as<size_t>();
    auto symbol = node["symbol"].as<std::string>();

    printf("HERE");
    auto decoded = MIO0Decoder::Decode(buffer, mio0);
    LUS::BinaryReader reader(decoded.data() + offset, sizeof(Lights1Raw));

    reader.SetEndianness(LUS::Endianness::Big);
    Lights1Raw lights;
    // Directional light

    // Diffuse
    auto r = reader.ReadInt8();
    auto g = reader.ReadInt8();
    auto b = reader.ReadInt8();
    auto a = reader.ReadInt8(); // unused

    // Diffuse copy
    auto r2 = reader.ReadInt8();
    auto g2 = reader.ReadInt8();
    auto b2 = reader.ReadInt8();
    auto a2 = reader.ReadInt8(); // unused

    // Direction
    auto x = reader.ReadInt8();
    auto y = reader.ReadInt8();
    auto z = reader.ReadInt8();
printf("HERE2");
    lights = {r, g, b, r2, g2, b2, x, y, z};
printf("HERE3");

    return std::make_shared<LightsData>(lights);
}