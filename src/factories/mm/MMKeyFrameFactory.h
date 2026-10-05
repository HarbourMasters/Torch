#pragma once

#include "factories/BaseFactory.h"

namespace MM {

class MMKeyFrameSkeletonBinaryExporter : public BaseExporter {
public:
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MMKeyFrameAnimationBinaryExporter : public BaseExporter {
public:
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MMKeyFrameSkeletonFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

private:
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, MMKeyFrameSkeletonBinaryExporter) };
    }
};

class MMKeyFrameAnimationFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

private:
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, MMKeyFrameAnimationBinaryExporter) };
    }
};

} // namespace MM
