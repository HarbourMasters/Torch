#include "ObjInitFactory.h"
#include "utils/Decompressor.h"
#include "Companion.h"

#define NUM(x, w) std::dec << std::setfill(' ') << std::setw(w) << x

void SF64::ObjInitCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto objs = std::static_pointer_cast<ObjInitData>(raw)->mObjInit;

    write << "ObjectInit " << symbol << "[] = {\n";
    for(auto& obj : objs) {
        auto enumName = Companion::Instance->GetEnumFromValue("ObjectId", obj.id).value_or(std::to_string(obj.id));
        write << fourSpaceTab << "{ ";
        write << NUM(obj.zPos1, 8) << "f, ";
        write << NUM(obj.zPos2, 7) << ", ";
        write << NUM(obj.xPos, 7) << ", ";
        write << NUM(obj.yPos, 7) << ", ";
        write << "{ " << NUM(obj.rot.x, 3)  << ", " << NUM(obj.rot.y, 3) << ", " << NUM(obj.rot.z, 3) << " }, ";
        write << enumName << " },\n";
    }
    write << "};\n\n";
}

void SF64::ObjInitBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<ObjInitData>(raw)->mObjInit;

    WriteHeader(writer, LUS::ResourceType::ObjectInit, 0);
    writer.Write((uint32_t) data.size());
    for(auto& obj : data) {
        writer.Write(obj.zPos1);
        writer.Write(obj.zPos2);
        writer.Write(obj.xPos);
        writer.Write(obj.yPos);
        writer.Write(obj.rot.x);
        writer.Write(obj.rot.y);
        writer.Write(obj.rot.z);
        writer.Write(obj.id);
    }
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SF64::ObjInitFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<ObjectInit> objects;

    bool processing = true;
    while(processing) {
        float zPos1 = reader.ReadFloat();
        int16_t zPos2 = reader.ReadInt16();
        int16_t xPos = reader.ReadInt16();
        int16_t yPos = reader.ReadInt16();
        int16_t rotX = reader.ReadInt16();
        int16_t rotY = reader.ReadInt16();
        int16_t rotZ = reader.ReadInt16();
        int16_t id = reader.ReadInt16();
        reader.ReadInt16();

        processing = id != -1;

        objects.push_back({ zPos1, zPos2, xPos, yPos, {rotX, rotY, rotZ}, id});
    }

    return std::make_shared<ObjInitData>(objects);
}