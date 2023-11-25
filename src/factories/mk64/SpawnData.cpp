#include "SpawnData.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/MIODecoder.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) "0x" << std::hex << std::setw(2) << std::setfill('0') << c

void MK64::SpawnDataHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = node["symbol"] ? node["symbol"].as<std::string>() : entryName;

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern ActorSpawnData " << symbol << "[];\n";
}

void MK64::SpawnDataCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto spawns = std::static_pointer_cast<SpawnDataData>(raw)->mSpawns;
    
    if (!node["symbol"]) {
        SPDLOG_ERROR("Asset in yaml missing entry for symbol.\nEx. symbol: myVariableNameHere");
        return;
    }

    auto symbol = node["symbol"].as<std::string>();

    write << "ActorSpawnData " << symbol << "[] = {\n";

    for (int i = 0; i < spawns.size(); i++) {
        auto x = spawns[i].x;
        auto y = spawns[i].y;
        auto z = spawns[i].z;

        auto id = spawns[i].id;

        if(i <= spawns.size() - 1) {
            write << fourSpaceTab;
        }

        // { x, y, z, id },
        write << "{" << NUM(x) << ", " << NUM(y) << ", " << NUM(z) << ", " << NUM(id) << " },\n";
    }
    write << "};\n\n";
}

void MK64::SpawnDataBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto spawns = std::static_pointer_cast<SpawnDataData>(raw)->mSpawns;
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::SpawnData, 0);
    writer.Write((uint32_t) spawns.size());
    for(auto s : spawns) {
        writer.Write(s.x);
        writer.Write(s.y);
        writer.Write(s.z);
        writer.Write(s.id);
    }

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> MK64::SpawnDataFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    
    if (!node["mio0"]) {
        SPDLOG_ERROR("yaml missing entry for mio0.\nEx. mio0: 0x10100");
        return std::nullopt;
    }

    if (!node["offset"]) {
        SPDLOG_ERROR("yaml missing entry for offset.\nEx. offset: 0x100");
        return std::nullopt;
    }

    if (!node["count"]) {
        SPDLOG_ERROR("Asset in yaml missing entry for vtx count.\nEx. count: 30\nThis means 30 vertices (an array size of 30).");
        return std::nullopt;
    }
    
    auto mio0 = node["mio0"].as<size_t>();
    auto offset = node["offset"].as<uint32_t>();
    auto count = node["count"].as<size_t>();

    auto decoded = MIO0Decoder::Decode(buffer, mio0);
    LUS::BinaryReader reader(decoded.data() + offset, count * sizeof(MK64::ActorSpawnData) );

    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<MK64::ActorSpawnData> spawns;

    for(size_t i = 0; i < count; i++) {
        auto x = reader.ReadInt16();
        auto y = reader.ReadInt16();
        auto z = reader.ReadInt16();
        auto id = reader.ReadUInt16();

        spawns.push_back(MK64::ActorSpawnData({
           x, y, z, id
       }));
    }

    return std::make_shared<SpawnDataData>(spawns);
}