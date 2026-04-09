#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include "OoTAnimationTypes.h"
#include <vector>
#include <string>

namespace OoT {

class OoTPlayerAnimationHeaderData : public IParsedData {
public:
    int16_t frameCount;
    std::string animDataPath;
};

class OoTPlayerAnimationHeaderBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTPlayerAnimationHeaderFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTPlayerAnimationHeaderBinaryExporter)
        };
    }
};

class OoTPlayerAnimationData : public IParsedData {
public:
    std::vector<int16_t> limbRotData;
};

class OoTPlayerAnimationDataBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTPlayerAnimationDataFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTPlayerAnimationDataBinaryExporter)
        };
    }
};

} // namespace OoT

#endif
