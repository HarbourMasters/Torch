#pragma once

#include "BaseFactory.h"
#include "audio/AudioManager.h"

class SampleData : public IParsedData {
public:
    AudioBankSample mSample;

    SampleData(AudioBankSample sample) : mSample(sample) {}
};

class SampleBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SampleFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, SampleBinaryExporter)
        };
    }
};