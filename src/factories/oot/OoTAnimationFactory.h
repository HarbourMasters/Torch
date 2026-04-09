#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include "OoTAnimationTypes.h"
#include <vector>
#include <optional>

namespace OoT {

class OoTNormalAnimationData : public IParsedData {
public:
    int16_t frameCount;
    std::vector<uint16_t> rotationValues;
    std::vector<RotationIndex> rotationIndices;
    int16_t limit;
    bool isLegacy = false;
};

class OoTAnimationBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTAnimationFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTAnimationBinaryExporter)
        };
    }
};

} // namespace OoT

#endif
