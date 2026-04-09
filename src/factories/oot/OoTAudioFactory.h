#pragma once

#ifdef OOT_SUPPORT

#include "OoTAudioTypes.h"

namespace OoT {

class OoTAudioData : public IParsedData {
public:
    std::vector<char> mMainEntry;
};

class OoTAudioBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTAudioFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTAudioBinaryExporter)
        };
    }

private:
    std::vector<char> BuildMainAudioHeader();
    std::optional<SafeAudioBankReader> LoadAudioBank(std::vector<uint8_t>& buffer);
    std::vector<AudioTableEntry> ParseAudioTable(const uint8_t* codeData, uint32_t tableOffset);
    std::vector<std::vector<uint8_t>> ParseSequenceFontTable(const uint8_t* codeData,
                                                              uint32_t tableOffset, uint32_t numSequences);
};

} // namespace OoT

#endif
