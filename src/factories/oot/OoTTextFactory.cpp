#include "OoTTextFactory.h"
#include "spdlog/spdlog.h"

// TODO: Reimplement — source was lost in filesystem damage.

namespace OoT {

std::optional<std::shared_ptr<IParsedData>> OoTTextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    SPDLOG_WARN("OoTTextFactory not yet reimplemented");
    return std::nullopt;
}

ExportResult OoTTextBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> data,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    return std::nullopt;
}

}  // namespace OoT
