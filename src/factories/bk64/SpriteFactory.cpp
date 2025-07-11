#include "SpriteFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <iomanip>
#include <yaml-cpp/yaml.h>
#include <cstring>
#include "archive/SWrapper.h"

extern "C" {
#include "n64graphics/n64graphics.h"
}

namespace BK64 {

static const std::unordered_map <std::string, TextureFormat> sTextureFormats = {
    { "RGBA16", { TextureType::RGBA16bpp, 16 } },
    { "RGBA32", { TextureType::RGBA32bpp, 32 } },
    { "CI4",    { TextureType::Palette4bpp, 4 } },
    { "CI8",    { TextureType::Palette8bpp, 8 } },
    { "I4",     { TextureType::Grayscale4bpp, 4 } },
    { "I8",     { TextureType::Grayscale8bpp, 8 } },
    { "IA1",    { TextureType::GrayscaleAlpha1bpp, 1 } },
    { "IA4",    { TextureType::GrayscaleAlpha4bpp, 4 } },
    { "IA8",    { TextureType::GrayscaleAlpha8bpp, 8 } },
    { "IA16",   { TextureType::GrayscaleAlpha16bpp, 16 } },
    { "TLUT",   { TextureType::TLUT, 16 } },
};

static const std::unordered_map <TextureType, std::string> sTextureFormatsReverse = {
    { TextureType::RGBA16bpp, "RGBA16" },
    { TextureType::RGBA32bpp, "RGBA32" },
    { TextureType::Palette4bpp, "CI4" },
    { TextureType::Palette8bpp, "CI8" },
    { TextureType::Grayscale4bpp, "I4" },
    { TextureType::Grayscale8bpp, "I8" },
    { TextureType::GrayscaleAlpha1bpp, "IA1" },
    { TextureType::GrayscaleAlpha4bpp, "IA4" },
    { TextureType::GrayscaleAlpha8bpp, "IA8" },
    { TextureType::GrayscaleAlpha16bpp, "IA16" },
    { TextureType::TLUT, "TLUT" },
};

#define ALIGN8(val) (((val) + 7) & ~7)

SpriteChunk ExtractTlut(LUS::BinaryReader& reader, uint32_t& offset, TextureType appliedFormat, const TextureFormat& format) {
    uint32_t width = 0;

    if (appliedFormat == TextureType::Palette4bpp) {
        width = 0x10;
    } else if (appliedFormat == TextureType::Palette8bpp) {
        width = 0x100;
    } else {
        SPDLOG_WARN("Bad TextureType Passed");
    }

    uint8_t* data = (uint8_t*)reader.ToVector().data() + offset;
    std::vector<uint8_t> buffer = std::vector(data, data + width * sizeof(int16_t));

    offset += width * sizeof(int16_t);

    return SpriteChunk(format, width, 1, buffer);
}

SpriteChunk ExtractChunk(LUS::BinaryReader& reader, uint32_t& offset, const TextureFormat& format) {
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    int16_t x = reader.ReadInt16();
    int16_t y = reader.ReadInt16();
    int16_t width = reader.ReadInt16();
    int16_t height = reader.ReadInt16();
    
    offset += 4 * sizeof(int16_t);
    offset = ALIGN8(offset);
    
    uint8_t* data = (uint8_t*)reader.ToVector().data() + offset;
    auto size = TextureUtils::CalculateTextureSize(format.type, width, height);
    std::vector<uint8_t> buffer = std::vector(data, data + size);

    offset += size;

    return SpriteChunk(format, width, height, buffer);
}

ExportResult SpriteHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {

    return std::nullopt;
}

ExportResult SpriteCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto sprite = std::static_pointer_cast<SpriteData>(raw);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    // TODO:

    return offset;
}

ExportResult SpriteBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto sprites = std::static_pointer_cast<SpriteData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKSprite, 0);
    
    auto wrapper = Companion::Instance->GetCurrentWrapper();
    uint32_t count = 0;
    uint32_t frame = 0;
    for (auto& chunk : sprites->mChunks) {
        std::ostringstream stream;

        auto texWriter = LUS::BinaryWriter();
        WriteHeader(texWriter, Torch::ResourceType::Texture, 0);

        texWriter.Write((uint32_t) chunk.mFormat.type);
        texWriter.Write(chunk.mWidth);
        texWriter.Write(chunk.mHeight);

        texWriter.Write((uint32_t) chunk.mBuffer.size());
        texWriter.Write((char*) chunk.mBuffer.data(), chunk.mBuffer.size());
        texWriter.Finish(stream);

        auto data = stream.str();
        if (chunk.mFormat.type != TextureType::TLUT) {
            wrapper->AddFile(entryName + "_" + std::to_string(frame) + "_" + std::to_string(count), std::vector(data.begin(), data.end()));
            count++;
        } else {
            wrapper->AddFile(entryName + "_" + std::to_string(frame) + "_TLUT", std::vector(data.begin(), data.end()));
        }
        if (count >= sprites->mChunkCounts.at(frame)) {
            count -= sprites->mChunkCounts.at(frame);
            frame++;
        }
    }

    writer.Write(frame);
    for (auto chunkCount : sprites->mChunkCounts) {
        writer.Write(chunkCount);
    }

    writer.Finish(write);

    return std::nullopt;
}

ExportResult SpriteModdingExporter::Export(std::ostream&write, std::shared_ptr<IParsedData> raw, std::string&entryName, YAML::Node&node, std::string* replacement) {
    auto sprites = std::static_pointer_cast<SpriteData>(raw);
    std::string file = *replacement;
    uint32_t count = 0;
    uint32_t frame = 0;
    SpriteChunk* palette = nullptr;

    *replacement += "." + sTextureFormatsReverse.at(sprites->mChunks.back().mFormat.type) + ".bin";

    for (auto& chunk : sprites->mChunks) {
        auto format = chunk.mFormat;
        std::ostringstream stream;
    
        uint8_t* raw = new uint8_t[TextureUtils::CalculateTextureSize(format.type, chunk.mWidth, chunk.mHeight) * 2];
        int size = 0;

        auto ext = sTextureFormatsReverse.at(format.type);

        std::transform(ext.begin(), ext.end(), ext.begin(), tolower);

        switch (format.type) {
            case TextureType::TLUT:
                // Set pallette and fall through to rgba case
                palette = &chunk;
            case TextureType::RGBA16bpp:
            case TextureType::RGBA32bpp: {
                rgba* imgr = raw2rgba(chunk.mBuffer.data(), chunk.mWidth, chunk.mHeight, format.depth);
                if(rgba2png(&raw, &size, imgr, chunk.mWidth, chunk.mHeight)) {
                    throw std::runtime_error("Failed to convert texture to PNG");
                }
                break;
            }
            case TextureType::GrayscaleAlpha16bpp:
            case TextureType::GrayscaleAlpha8bpp:
            case TextureType::GrayscaleAlpha4bpp:
            case TextureType::GrayscaleAlpha1bpp: {
                ia* imgia = raw2ia(chunk.mBuffer.data(), chunk.mWidth, chunk.mHeight, format.depth);
                if(ia2png(&raw, &size, imgia, chunk.mWidth, chunk.mHeight)) {
                    throw std::runtime_error("Failed to convert texture to PNG");
                }
                break;
            }
            case TextureType::Palette8bpp:
            case TextureType::Palette4bpp: {
                if (palette == nullptr) {
                    throw std::runtime_error("Failed to find TLUT");
                }
                convert_raw_to_ci8(&raw, &size, chunk.mBuffer.data(), (uint8_t *)palette->mBuffer.data(), 0, chunk.mWidth, chunk.mHeight, chunk.mFormat.depth, palette->mFormat.depth);
                break;
            }
            case TextureType::Grayscale8bpp:
            case TextureType::Grayscale4bpp: {
                ia* imgi = raw2i(chunk.mBuffer.data(), chunk.mWidth, chunk.mHeight, format.depth);
                if(ia2png(&raw, &size, imgi, chunk.mWidth, chunk.mHeight)) {
                    throw std::runtime_error("Failed to convert texture to PNG");
                }
                break;
            }
            default: {
                SPDLOG_ERROR("Unsupported texture format for modding: {}", ext);
            }
        }

        std::string outFile;
        if (format.type != TextureType::TLUT) {
            outFile = file + "_" + std::to_string(frame) + "_" + std::to_string(count) + "." + ext + ".png";
            count++;
        } else {
            outFile = file + "_" + std::to_string(frame) + "_TLUT" + "." + ext + ".png";
        }

        if (count >= sprites->mChunkCounts.at(frame)) {
            count -= sprites->mChunkCounts.at(frame);
            frame++;
        }

        stream.write(reinterpret_cast<char*>(raw), size);
        auto data = stream.str();
        if(data.empty()) {
            continue;
        }

        std::string dpath = Companion::Instance->GetOutputPath() + "/" + outFile;
        if(!exists(fs::path(dpath).parent_path())){
            create_directories(fs::path(dpath).parent_path());
        }

        std::ofstream file(dpath, std::ios::binary);
        file.write(data.c_str(), data.size());
        file.close();
    }
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SpriteFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    uint32_t offset;
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    
    reader.SetEndianness(Torch::Endianness::Big);
    
    int16_t frameCount = reader.ReadInt16();
    int16_t formatCode = reader.ReadInt16();
    std::string format;

    switch (formatCode) {
        case 0x1:
            format = "CI4";
            break;
        case 0x4:
            format = "CI8";
            break;
        case 0x20:
            format = "I4";
            break;
        case 0x40:
            format = "I8";
            break;
        case 0x100:
            format = "IA8";
            break;
        case 0x400:
            format = "RGBA16";
            break;
        case 0x800:
            format = "RGBA32";
            break;
        default:
            SPDLOG_WARN("UNRECOGNISED FORMAT 0x{:X}", formatCode);
            return std::nullopt;
    }

    std::vector<SpriteChunk> sprites;
    std::vector<uint16_t> chunkCounts;

    if (frameCount > 0x100) {
        offset = 8;
        sprites.emplace_back(ExtractChunk(reader, offset, sTextureFormats.at("RGBA16")));
        chunkCounts.push_back(1);
        return std::make_shared<SpriteData>(sprites, chunkCounts);
    }

    reader.Seek(0x10, LUS::SeekOffsetType::Start);
    std::vector<uint32_t> offsets;
    for (int16_t i = 0; i < frameCount; i++) {
        offsets.push_back(reader.ReadUInt32());
    }

    for (const auto &frameOffset : offsets) {
        offset = 0x10 + frameOffset + frameCount * sizeof(uint32_t);
        reader.Seek(offset, LUS::SeekOffsetType::Start);
        int16_t x = reader.ReadInt16();
        int16_t y = reader.ReadInt16();
        int16_t width = reader.ReadInt16();
        int16_t height = reader.ReadInt16();
        uint16_t chunkCount = reader.ReadInt16();

        offset += 0x14;

        chunkCounts.push_back(chunkCount);
        
        if (format == "CI4" || format == "CI8") {
            offset = ALIGN8(offset);
            sprites.emplace_back(ExtractTlut(reader, offset, sTextureFormats.at(format).type, sTextureFormats.at("TLUT")));
        }

        for (uint16_t i = 0; i < chunkCount; i++) {
            sprites.emplace_back(ExtractChunk(reader, offset, sTextureFormats.at(format)));
        }
    }

    return std::make_shared<SpriteData>(sprites, chunkCounts);
}

} // namespace BK64
