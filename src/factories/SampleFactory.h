#pragma once

#include <utility>

#include "BaseFactory.h"
#include "audio/AudioManager.h"

class SampleData : public IParsedData {
public:
    AudioBankSample mSample;

    explicit SampleData(AudioBankSample sample) : mSample(std::move(sample)) {}
};

class SampleBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SampleFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, SampleBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};