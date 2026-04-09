#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include "OoTAnimationTypes.h"
#include <vector>

namespace OoT {

class OoTCurveAnimationData : public IParsedData {
public:
    int16_t frameCount;
    std::vector<uint8_t> refIndexArr;
    std::vector<CurveInterpKnot> transformDataArr;
    std::vector<int16_t> copyValuesArr;
};

class OoTCurveAnimationBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTCurveAnimationFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTCurveAnimationBinaryExporter)
        };
    }
};

} // namespace OoT

#endif
