#include "RuntimeBufferFactory.h"

namespace AC {

ExportResult RuntimeBufferBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> data,
                                                 std::string& entryName, YAML::Node& node, std::string* replacement) {
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> RuntimeBufferFactory::parse(std::vector<uint8_t>& buffer,
                                                                        YAML::Node& node) {
    GetSafeNode<uint32_t>(node, "size");
    GetSafeNode<uint32_t>(node, "segment");
    GetSafeNode<uint32_t>(node, "segment_offset");
    node["no_export"] = true;
    return std::make_shared<IParsedData>();
}

} // namespace AC
