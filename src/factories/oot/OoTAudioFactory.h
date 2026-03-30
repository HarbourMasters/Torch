#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"

namespace OoT {

class OoTAudioData : public IParsedData {
public:
    std::vector<char> mMainEntry; // The 64-byte OAUD header (empty body)
    // Future: companion file data for samples, fonts, sequences
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
};

} // namespace OoT

#endif
