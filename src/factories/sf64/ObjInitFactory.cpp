#include "ObjInitFactory.h"
#include "utils/Decompressor.h"
#include "Companion.h"
#include <iomanip>

void SF64::ObjInitCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto objs = std::static_pointer_cast<ObjInitData>(raw)->mObjInit;

    write << "ObjectInit " << symbol << "[] = {\n";
    for(auto& obj : objs) {
        auto enumName = Companion::Instance->GetEnumFromValue("obj_id", obj.id).value_or(std::to_string(obj.id));
        write << fourSpaceTab << "{ " << obj.zPos1 << ", ";
        write << obj.zPos2 << ", ";
        write << obj.xPos << ", ";
        write << obj.yPos << ", ";
        write << "{ " << std::hex << obj.rot.x  << ", " << std::hex << obj.rot.y << ", " << std::hex << obj.rot.z << " }, ";
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
    auto count = GetSafeNode<size_t>(node, "count");

    LUS::BinaryReader reader(segment.data, sizeof(ObjectInit) * count);
    reader.SetEndianness(LUS::Endianness::Big);
    std::vector<ObjectInit> objects;

    for (int i = 0; i < count; i++) {

        float zPos1 = reader.ReadFloat();
        int16_t zPos2 = reader.ReadInt16();
        int16_t xPos = reader.ReadInt16();
        int16_t yPos = reader.ReadInt16();
        int16_t rotX = reader.ReadInt16();
        int16_t rotY = reader.ReadInt16();
        int16_t rotZ = reader.ReadInt16();
        int16_t id = reader.ReadInt16();
        reader.ReadInt16(); // padding

        ObjectInit objInit{
            zPos1, zPos2,
            xPos, yPos,
            { rotX, rotY, rotZ },
            id
        };

        objects.push_back(objInit);
    }


    return std::make_shared<ObjInitData>(objects);
}