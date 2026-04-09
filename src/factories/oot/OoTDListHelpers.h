#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <string>
#include <optional>
#include <tuple>
#include <vector>

namespace OoT {
namespace DListHelpers {

// OoT replacement for SearchVtx. Returns result if handled, nullopt to fall through to main.
std::optional<std::tuple<std::string, YAML::Node>> SearchVtx(uint32_t ptr);

// OoT replacement for DListBinaryExporter::Export. Returns result if handled, nullopt to fall through to main.
std::optional<ExportResult> Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                  std::string& entryName, YAML::Node& node, std::string* replacement);

// OoT replacement for DListFactory::parse. Returns result if handled, nullopt to fall through to main.
std::optional<std::optional<std::shared_ptr<IParsedData>>> Parse(
    std::vector<uint8_t>& raw_buffer, YAML::Node& node);

} // namespace DListHelpers
} // namespace OoT

#endif
