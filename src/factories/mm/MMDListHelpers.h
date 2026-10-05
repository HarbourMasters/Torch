#pragma once

#include "factories/BaseFactory.h"

namespace MM {
namespace DListHelpers {

std::optional<ExportResult> Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                   std::string& entryName, YAML::Node& node, std::string* replacement);

} // namespace DListHelpers
} // namespace MM
