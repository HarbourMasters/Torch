#pragma once

#include "factories/BaseFactory.h"

#include <string>
#include <vector>

namespace AC {

struct KeyFrame {
    int16_t frame;
    int16_t value;
    int16_t velocity;
};

struct KeyFrameCounts {
    uint32_t keys;
    uint32_t fixed;
};

class KeyFrameAnimData : public IParsedData {
public:
    std::vector<uint8_t> mFlags;
    std::vector<KeyFrame> mKeyFrames;
    std::vector<int16_t> mKeyCounts;
    std::vector<int16_t> mFixedValues;
    int16_t mPad = 0;
    int16_t mFrames = 0;
};

KeyFrameCounts CountKeyFrameTables(const std::vector<uint8_t>& flags);
KeyFrameAnimData ReadKeyFrameAnim(uint32_t addr, YAML::Node& node, const std::string& name);
void WriteKeyFrameAnimBody(LUS::BinaryWriter& writer, const KeyFrameAnimData& anim);

class KeyFrameAnimBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class KeyFrameAnimFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, KeyFrameAnimBinaryExporter)
        };
    }
};

} // namespace AC
