#pragma once

#include "factories/BaseFactory.h"
#include "utils/TextureUtils.h"
#include <types/RawBuffer.h>
#include <unordered_map>
#include <string>
#include <vector>

namespace BK64 {

class SpriteChunk {
public:
    TextureFormat mFormat;
    uint32_t mWidth;
    uint32_t mHeight;
    std::vector<uint8_t> mBuffer;

    SpriteChunk(TextureFormat format, uint32_t width, uint32_t height, std::vector<uint8_t>& buffer) : mFormat(format), mWidth(width), mHeight(height), mBuffer(std::move(buffer)) {}
};

class SpriteData : public IParsedData {
public:
    std::vector<SpriteChunk> mChunks;
    std::vector<uint16_t> mChunkCounts;

    SpriteData(std::vector<SpriteChunk> chunks, std::vector<uint16_t> chunkCounts) : mChunks(std::move(chunks)), mChunkCounts(std::move(chunkCounts)) {}
};

class SpriteHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, SpriteHeaderExporter)
            REGISTER(Binary, SpriteBinaryExporter)
            REGISTER(Code, SpriteCodeExporter)
            REGISTER(Modding, SpriteModdingExporter)
        };
    }

    bool SupportModdedAssets() override { return true; }
};

} // namespace BK64
