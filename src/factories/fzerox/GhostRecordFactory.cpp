#include "GhostRecordFactory.h"

#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

ExportResult FZX::GhostRecordHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern GhostRecord " << symbol << ";\n";

    return std::nullopt;
}

ExportResult FZX::GhostRecordCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto record = std::static_pointer_cast<GhostRecordData>(raw);

    write << "GhostRecord " << symbol << " = {\n";

    return offset;
}

ExportResult FZX::GhostRecordBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto record = std::static_pointer_cast<GhostRecordData>(raw);

    WriteHeader(writer, Torch::ResourceType::GhostRecord, 0);

    writer.Finish(write);
    return std::nullopt;
}

ExportResult FZX::GhostRecordModdingExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto record = std::static_pointer_cast<GhostRecordData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    std::stringstream stream;
    YAML::Emitter out;

    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;

    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> FZX::GhostRecordFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);

    reader.SetEndianness(Torch::Endianness::Big);

    return std::make_shared<GhostRecordData>();
}

std::optional<std::shared_ptr<IParsedData>> FZX::GhostRecordFactory::parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) {
    YAML::Node assetNode;

    try {
        std::string text((char*) buffer.data(), buffer.size());
        assetNode = YAML::Load(text.c_str());
    } catch (YAML::ParserException& e) {
        SPDLOG_ERROR("Failed to parse message data: {}", e.what());
        SPDLOG_ERROR("{}", (char*) buffer.data());
        return std::nullopt;
    }

    const auto info = assetNode.begin()->second;

    return std::make_shared<GhostRecordData>();
}
