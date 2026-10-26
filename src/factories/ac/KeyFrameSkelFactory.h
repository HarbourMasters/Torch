#pragma once

#include "factories/BaseFactory.h"

#include <string>
#include <vector>

namespace AC {

struct KeyFrameLimb {
    std::string path;
    uint8_t child;
    uint8_t flags;
    int16_t x;
    int16_t y;
    int16_t z;
};

class KeyFrameSkelData : public IParsedData {
public:
    uint8_t mLimbCount;
    uint8_t mDListCount;
    std::vector<KeyFrameLimb> mLimbs;

    KeyFrameSkelData(uint8_t limbCount, uint8_t dListCount, std::vector<KeyFrameLimb> limbs)
        : mLimbCount(limbCount), mDListCount(dListCount), mLimbs(std::move(limbs)) {}
};

class KeyFrameSkelBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class KeyFrameSkelFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, KeyFrameSkelBinaryExporter)
        };
    }
};

} // namespace AC
