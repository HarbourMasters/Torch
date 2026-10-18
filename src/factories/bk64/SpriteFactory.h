#pragma once

#include "factories/BaseFactory.h"
#include "factories/bk64/BKHeaderExporter.h"
#include "factories/bk64/BKImage.h"
#include "utils/TextureUtils.h"
#include <functional>
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

struct SpriteChunk {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    std::vector<uint8_t> texels; // as the ROM stores them
};

struct SpriteFrame {
    SpriteFrameHeader header;
    std::vector<uint8_t> tlut; // big-endian RGBA5551, 16 colors for CI4 and 256 for CI8
    std::vector<SpriteChunk> chunks;
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
    std::vector<SpriteFrame> mFrames;                    // empty for a sprite read without its texels
    // Built from modding files, so the binary export writes the chunk textures too
    bool mWriteTextures = false;

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

using SpriteFileReader = std::function<bool(const std::string& name, std::vector<uint8_t>& out)>;

struct SpriteFile {
    std::string name;
    std::vector<uint8_t> bytes;
};

// "CI4", "RGBA16" and so on, or nullptr for a code the game doesn't draw.
const char* SpriteFormatName(int16_t code);
// Null for the two raw textures stored where sprites go (a frame count over 0x100).
std::shared_ptr<SpriteData> ReadSpriteRom(const std::vector<uint8_t>& bytes, std::string& error);
std::vector<uint8_t> WriteSpriteRom(const SpriteData& sprite);
// textures reads the "_<frame>_<chunk>" and "_<frame>_TLUT" resources beside the sprite.
std::shared_ptr<SpriteData> ReadSpriteResource(const std::vector<uint8_t>& resource, const SpriteFileReader& textures,
                                               std::string& error);
// The sprite resource first, under "", then each texture resource under its suffix.
std::vector<SpriteFile> WriteSpriteResource(const SpriteData& sprite);
// images gets the frame images the yaml names, relative to the yaml.
std::string SpriteToYaml(const SpriteData& sprite, const std::string& symbol, std::vector<SpriteFile>& images);
// files reads the images the yaml names.
bool SpriteFromYaml(const std::string& text, const SpriteFileReader& files, SpriteData& out, std::string& error);

// A frame drawn on a canvas that covers the frame and all its chunks.
struct SpriteCanvas {
    RgbaImage image;
    int16_t originX = 0; // the frame point at the canvas's top-left
    int16_t originY = 0;
};
SpriteCanvas DrawSpriteFrame(const SpriteData& sprite, size_t frame);
// New art for a frame. The pivot scales with it, and the chunks and bounds are redone. oneChunk keeps the frame
// in one piece, which HUD sprites need.
void SetSpriteFrameImage(SpriteData& sprite, size_t frame, const RgbaImage& image, bool oneChunk);
// Art drawn on the frame's own canvas, as DrawSpriteFrame gives it. The chunks keep their places and the bounds are
// redone. False when the canvas isn't that size.
bool RedrawSpriteFrame(SpriteData& sprite, size_t frame, const RgbaImage& canvas);
// Fills the frame count, chunk counts, positions and frame headers in from mFrames.
void LayoutSprite(SpriteData& sprite);

class SpriteFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Header, BKHeaderExporter) REGISTER(Binary, SpriteBinaryExporter)
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
