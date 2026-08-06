#pragma once

#include "Companion.h"
#include <factories/BaseFactory.h>

namespace BK64 {

class BKHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override {
        const auto symbol = GetSafeNode(node, "symbol", entryName);

        if (Companion::Instance->IsOTRMode()) {
            write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        }

        return std::nullopt;
    }
};

} // namespace BK64
