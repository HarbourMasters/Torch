#pragma once

#include <utility>

#include <factories/BaseFactory.h>
#include "AudioManager.h"

class SampleModdingExporter : public BaseExporter {
public:
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement);
};

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
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            // REGISTER(Modding, SampleModdingExporter)
            REGISTER(Binary, SampleBinaryExporter)
        };
    }

    bool SupportModdedAssets() override { return true; }
};

#ifdef BUILD_UI
// Decodes a sample to PCM16 through the AIFF converter. The PCM is 1:1 with
// the vadpcm frames (front-padded so loop points stay aligned); `rate` is the
// tuning-derived natural rate.
bool DecodeSampleToPcm(AudioBankSample* sample, std::vector<int16_t>& pcm, int& rate);

// Parses an in-memory AIFF (as produced by write_aiff) into PCM16.
bool DecodeAiffBytes(const std::vector<char>& bytes, std::vector<int16_t>& pcm, int& rate);

// Plays the sample (decoded through the AIFF converter) with basic controls.
class SampleFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};
#endif
