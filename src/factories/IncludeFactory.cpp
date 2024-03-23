#include "IncludeFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) std::dec << std::setfill(' ') << std::setw(3) << c

ExportResult IncludeHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto ctype = GetSafeNode<size_t>(node, "ctype");

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern " << ctype << " " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult IncludeCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto sym = GetSafeNode<size_t>(node, "symbol");
    auto file = GetSafeNode<size_t>(node, "file_path");
    auto ctype = GetSafeNode<size_t>(node, "ctype");

    write << ctype << " " << symbol << "[] = {\n";

    write << "#include '" << file << "'";

    write << "};\n";

    return std::nullopt;
}

ExportResult IncludeBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    throw std::runtime_error("Include factory should not be used for otr/o2r mode.");
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> IncludeFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {

    const uint32_t blank = 0;

    return std::make_shared<IncludeData>(blank);
}