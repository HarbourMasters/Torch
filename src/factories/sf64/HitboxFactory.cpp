#include "HitboxFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

SF64::HitboxData::HitboxData(std::vector<float> data, std::vector<int> types): mData(data), mTypes(types) {
    
}

void SF64::HitboxHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern f32 " << symbol << "[];\n";
}

void SF64::HitboxCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto hitbox = std::static_pointer_cast<SF64::HitboxData>(raw);
    auto index = 0;
    auto count = hitbox->mData[index++];
    auto off = offset;
    if(IS_SEGMENTED(off)) {
        off = SEGMENT_OFFSET(off);
    } 

    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::uppercase << std::hex << off << "\n";
    }
    write << "f32 " << symbol << "[] = {\n";
    write << fourSpaceTab << count << ",\n";
    
    for(int i = 0; i < (int)count; i++) {
        if(hitbox->mTypes[i]  == 4) {
            write << fourSpaceTab << "HITBOX_TYPE_4,   ";
            index++;
        } else if (hitbox->mTypes[i] == 3) {
            write << fourSpaceTab << "HITBOX_TYPE_3,   ";
            index++;
        } else if (hitbox->mTypes[i] == 2) {
            write << fourSpaceTab << "HITBOX_TYPE_2,   ";
            index++;
            for(int j = 0; j < 3; j++) {
                write << hitbox->mData[index++] << ", ";
            }
            write << fourSpaceTab;
        } else {
            write << " /* HITBOX_TYPE_1 */ ";
        }
        for(int j = 0; j < 6; j++) {
            write << hitbox->mData[index++] << ", ";
        }
        write << "\n";
    }
    write << "};\n";
    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::uppercase << std::hex << off + sizeof(float) * index << "\n";
    }
    write << "\n";
}

void SF64::HitboxBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto hitbox = std::static_pointer_cast<SF64::HitboxData>(raw);
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::Hitbox, 0);
    for (float value : hitbox->mData) {
        writer.Write(value);
    }
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SF64::HitboxFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    std::vector<float> data;
    int count;
    std::vector<int> types;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x10000);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    data.push_back(reader.ReadFloat());
    count = data[0];

    while(count > 0) {
        auto typecode = reader.ReadFloat();
        auto readCount = 0;
        if(typecode == 300000.0f || typecode == 400000.0f) {
            readCount = 6;
            types.push_back((typecode == 300000.0f) ? 3 : 4);
        } else if (typecode == 200000.0f) {
            readCount = 9;
            types.push_back(2);
        } else {
            readCount = 5;
            types.push_back(1);
        }
        data.push_back(typecode);
        for(int i = 0; i < readCount; i++) {
            data.push_back(reader.ReadFloat());
        }
        count--;
    }

    return std::make_shared<SF64::HitboxData>(data, types);
}
