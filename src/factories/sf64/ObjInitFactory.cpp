#include "ObjInitFactory.h"
#include "utils/Decompressor.h"
#include "Companion.h"

#define NUM(x, w) std::dec << std::setfill(' ') << std::setw(w) << x
#define FLOAT(x, w) std::dec << std::setfill(' ') << std::setw(w) << std::fixed << std::setprecision(1) << x << "f"

ExportResult SF64::ObjInitHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern ObjectInit " << symbol << "[];\n";
}

ExportResult SF64::ObjInitCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto objs = std::static_pointer_cast<ObjInitData>(raw)->mObjInit;

    if (Companion::Instance->IsDebug()) {
        if (IS_SEGMENTED(offset)) {
            offset = SEGMENT_OFFSET(offset);
        }
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

    write << "ObjectInit " << symbol << "[] = {\n";
    for(auto& obj : objs) {
        auto enumName = Companion::Instance->GetEnumFromValue("ObjectId", obj.id).value_or(std::to_string(obj.id));
        write << fourSpaceTab << "{ ";
        write << FLOAT(obj.zPos1, 8) << ", ";
        write << NUM(obj.zPos2, 7) << ", ";
        write << NUM(obj.xPos, 7) << ", ";
        write << NUM(obj.yPos, 7) << ", ";
        write << NUM(obj.rot, 3) << ", ";
        write << enumName << " },\n";
    }
    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// count: " << std::to_string(objs.size()) << " ObjectInits\n";
    }

    return (IS_SEGMENTED(offset) ? SEGMENT_OFFSET(offset) : offset) + sizeof(ObjectInit) * objs.size();
}

ExportResult SF64::ObjInitBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
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
    bool terminator = false;

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

        if(id == -1) {
            terminator = true;
        }
        if(terminator && ((zPos1*zPos2*xPos*yPos*rotX*rotY*rotZ) != 0 || id != -1)) {
            processing = false;
        } else {
            objects.push_back({ zPos1, zPos2, xPos, yPos, {rotX, rotY, rotZ}, id});
        }
    }

    return std::make_shared<ObjInitData>(objects);
}