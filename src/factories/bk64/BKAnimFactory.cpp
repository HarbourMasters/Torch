#include "BKAnimFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

namespace BK64{

ExportResult AnimHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern BK64::Anim " << symbol << ";\n";

    return std::nullopt;
}

ExportResult AnimCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<AnimData>(raw);

}

ExportResult AnimBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {

}

std::optional<std::shared_ptr<IParsedData>> AnimFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [root, segment] = Decompressor::AutoDecode(node, buffer, 0x1000);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    return std::make_shared<AnimData>();
}
}