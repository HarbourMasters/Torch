#include "MessageLookupFactory.h"

#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

void SF64::MessageLookupHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern MsgLookup " << symbol << "[];\n";
}

void SF64::MessageLookupCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto table = std::static_pointer_cast<MessageTable>(raw)->mTable;
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    if (IS_SEGMENTED(offset)) {
        offset = SEGMENT_OFFSET(offset);
    }

    if (Companion::Instance->IsDebug()) {
        if (IS_SEGMENTED(offset)) {
            offset = SEGMENT_OFFSET(offset);
        }
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

    write << "// clang-format on\n";
    write << "MsgLookup " << symbol << "[] = {\n" << fourSpaceTab;
    for (int i = 0; i < table.size(); ++i) {
        auto m = table[i];
        if(i % 4 == 0 && i != 0){
            write << "\n" << fourSpaceTab;
        }

        auto dec = Companion::Instance->GetNodeByAddr(m.ptr);
        std::string msgSymbol = "NULL";

        if(dec.has_value()){
            auto node = std::get<1>(dec.value());
            msgSymbol = GetSafeNode<std::string>(node, "symbol");
        }

        write << "{ " << m.id << std::dec << ", " << msgSymbol << " }, ";
    }

    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::hex << std::uppercase << (offset + (table.size() * sizeof(uint16_t))) << "\n";
    }

    write << "\n";
}

void SF64::MessageLookupBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    const auto data = std::static_pointer_cast<MessageTable>(raw);

    WriteHeader(writer, LUS::ResourceType::MessageTable, 0);

    writer.Write(static_cast<uint32_t>(data->mTable.size()));
    for(auto m : data->mTable) {
        writer.Write(m.id);
        writer.Write(m.ptr);
    }
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SF64::MessageLookupFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto vram = GetSafeNode<uint32_t>(node, "vram");
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    std::vector<MessageEntry> message;

    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    int32_t id = reader.ReadInt32();
    uint32_t ptr = reader.ReadInt32();

    while(id != -1) {

        YAML::Node entry;

        ptr = (SEGMENT_NUMBER(offset) << 24) | (ptr - vram);
        std::string output = "gMsg_ID_" + std::to_string(id);
        entry["type"] = "SF64:MESSAGE";
        entry["offset"] = ptr;
        entry["symbol"] = output;
        entry["autogen"] = true;
        SPDLOG_INFO("Message ID: {} Ptr: {:X} Offset: {:X}", id, ptr, (SEGMENT_NUMBER(offset) << 24) | (ptr - vram));

        Companion::Instance->RegisterAsset(output, entry);
        message.push_back({id, ptr});
        id = reader.ReadInt32();
        ptr = reader.ReadInt32();
    }

    message.push_back({-1, 0});

    return std::make_shared<MessageTable>(message);
}