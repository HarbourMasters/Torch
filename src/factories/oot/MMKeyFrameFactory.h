#pragma once

#include "factories/BaseFactory.h"

namespace OoT {

// Majora's Mask keyframe skeletons and animations. OoT has neither, so these are
// MM-only rather than a branch inside a shared factory. Mirrors OTRExporter's
// CKeyFrameExporter.cpp and ZAPD's ZCKeyFrame / ZCKeyFrameAnim.

class MMKeyFrameSkelBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MMKeyFrameSkelFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, MMKeyFrameSkelBinaryExporter) };
    }
};

class MMKeyFrameAnimBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MMKeyFrameAnimFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, MMKeyFrameAnimBinaryExporter) };
    }
};

} // namespace OoT
