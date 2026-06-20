#pragma once

#include "factories/BaseFactory.h"
#include "utils/TextureUtils.h"
#include <string>
#include <types/RawBuffer.h>
#include <unordered_map>
#include <vector>

namespace BK64 {

/**
 * One frame's header out of the ROM sprite struct. This is BKSpriteFrame in the decomp.
 */
struct SpriteFrameHeader {
    int16_t x; // unk0 — X origin offset
    int16_t y; // unk2 — Y origin offset
    int16_t w; // frame width
    int16_t h; // frame height
    int16_t unkA;
    int16_t unkC;
    int16_t unkE;
    int16_t unk10;
    int16_t unk12;
};

/**
 * A 2D billboard sprite.
 */
class SpriteData : public IParsedData {
  public:
    int16_t mFrameCount; // animation frame count
    int16_t mFormatCode; // texture format (RGBA16=0, RGBA32=1, CI4=2, CI8=3, etc.)
    int16_t mUnk4;       // ROM header field at offset 4
    int16_t mUnk6;       // ROM header field at offset 6
    int16_t mUnk8;       // display width, drives billboard vertex positioning
    int16_t mUnkA;       // display height, same
    // unkC bitfield: the animation params, a BE u32 at ROM offset 0x0C
    uint8_t mAnimSpeed;                                  // bits 31-28: 4 bits — animation speed divisor
    uint8_t mAnimType;                                   // bits 27-25: 3 bits — animation type (0=none, 1-4=various
                                                         // loop modes)
    uint8_t mAnimDirection;                              // bits 24-23: 2 bits — animation direction control
    uint8_t mAnimFlip;                                   // bits 22-21: 2 bits — flip/mirror control
    std::vector<uint16_t> mChunkCounts;                  // chunks per frame (length = mFrameCount)
    std::vector<std::pair<int16_t, int16_t>> mPositions; // (x, y) offset per chunk
    std::vector<SpriteFrameHeader> mFrameHeaders;        // per-frame header data

    SpriteData(int16_t frameCount, int16_t formatCode, std::vector<uint16_t> chunkCounts,
               std::vector<std::pair<int16_t, int16_t>> positions, std::vector<SpriteFrameHeader> frameHeaders = {},
               int16_t unk4 = 0, int16_t unk6 = 0, int16_t unk8 = 0, int16_t unkA = 0, uint8_t animSpeed = 0,
               uint8_t animType = 0, uint8_t animDirection = 0, uint8_t animFlip = 0)
        : mFrameCount(frameCount), mFormatCode(formatCode), mUnk4(unk4), mUnk6(unk6), mUnk8(unk8), mUnkA(unkA),
          mAnimSpeed(animSpeed), mAnimType(animType), mAnimDirection(animDirection), mAnimFlip(animFlip),
          mChunkCounts(std::move(chunkCounts)), mPositions(std::move(positions)),
          mFrameHeaders(std::move(frameHeaders)) {
    }
};

class SpriteHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node,
                        std::string* replacement) override;
};

class SpriteBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node,
                        std::string* replacement) override;
};

class SpriteCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node,
                        std::string* replacement) override;
};

class SpriteModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node,
                        std::string* replacement) override;
};

class SpriteFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Header, SpriteHeaderExporter) REGISTER(Binary, SpriteBinaryExporter)
                     REGISTER(Code, SpriteCodeExporter) REGISTER(Modding, SpriteModdingExporter) };
    }

    bool HasModdedDependencies() override {
        return true;
    }
    bool SupportModdedAssets() override {
        return true;
    }
};

} // namespace BK64
