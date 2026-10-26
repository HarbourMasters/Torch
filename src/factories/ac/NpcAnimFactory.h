#pragma once

#include "factories/BaseFactory.h"
#include "KeyFrameAnimFactory.h"

#include <vector>

namespace AC {

struct NpcSeData {
    bool present = false;
    int32_t numCheckFrames = 0;
    std::vector<int32_t> frames;
};

struct NpcOtherSeData {
    NpcSeData se;
    uint16_t seNo;
};

class NpcAnimData : public IParsedData {
public:
    KeyFrameAnimData mAnim;
    float mStartFrame;
    float mEndFrame;
    int32_t mMode;
    float mMorphCounter;
    std::vector<uint8_t> mEyeSeq;
    int16_t mEyeSeqType;
    int16_t mEyeSeqStopFrame;
    std::vector<uint8_t> mMouthSeq;
    int16_t mMouthSeqType;
    int16_t mMouthSeqStopFrame;
    int16_t mFeelEffectSetFrame;
    int16_t mFeelEffectType;
    bool mHasFeel = false;
    int16_t mFeelType = 0;
    uint8_t mFeelMax = 0;
    uint8_t mFeelSetNum = 0;
    std::vector<uint32_t> mFeelSet;
    bool mHasSe = false;
    NpcSeData mLFoot;
    NpcSeData mRFoot;
    int32_t mOtherSeType = 0;
    std::vector<NpcOtherSeData> mOtherSe;
};

class NpcAnimBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class NpcAnimFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, NpcAnimBinaryExporter)
        };
    }
};

} // namespace AC
