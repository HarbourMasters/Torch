#include "TemplateFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

TypeData::TypeData(uint32_t data): mData(data) {
    
}

void TypeHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern Type " << symbol << ";\n";
    // write << "extern Type " << symbol << "[];\n";
}

void TypeCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<TypeData>(raw);
    
}

void TypeBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {

}

std::optional<std::shared_ptr<IParsedData>> TypeFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {

    return std::make_shared<TypeData>();
}
