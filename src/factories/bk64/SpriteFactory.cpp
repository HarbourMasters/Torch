#include "SpriteFactory.h"
#include "Companion.h"
#include "archive/SWrapper.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"
#include <cstring>
#include <iomanip>
#include <yaml-cpp/yaml.h>
extern "C" {
#include "n64graphics/n64graphics.h"
}

namespace BK64 {

static const std::unordered_map<std::string, std::string> sTextureCTypes = {
    { "RGBA16", "u16" }, { "RGBA32", "u16" }, { "CI4", "u8" }, { "CI8", "u8" },   { "I4", "u8" },    { "I8", "u8" },
    { "IA1", "u8" },     { "IA4", "u8" },     { "IA8", "u8" }, { "IA16", "u16" }, { "TLUT", "u16" },
};

static const std::unordered_map<std::string, TextureType> sTextureFormats = {
    { "RGBA16", TextureType::RGBA16bpp },
    { "RGBA32", TextureType::RGBA32bpp },
    { "CI4", TextureType::Palette4bpp },
    { "CI8", TextureType::Palette8bpp },
    { "I4", TextureType::Grayscale4bpp },
    { "I8", TextureType::Grayscale8bpp },
    { "IA1", TextureType::GrayscaleAlpha1bpp },
    { "IA4", TextureType::GrayscaleAlpha4bpp },
    { "IA8", TextureType::GrayscaleAlpha8bpp },
    { "IA16", TextureType::GrayscaleAlpha16bpp },
    { "TLUT", TextureType::TLUT },
};

#define ALIGN8(val) (((val) + 7) & ~7)

void ExtractChunk(LUS::BinaryReader& reader, std::vector<std::pair<int16_t, int16_t>>& positions, uint32_t& offset,
                  std::string format, std::string symbol, uint32_t chunkNo) {
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    int16_t x = reader.ReadInt16();
    int16_t y = reader.ReadInt16();
    int16_t width = reader.ReadInt16();
    int16_t height = reader.ReadInt16();

    positions.emplace_back(x, y);

    offset += 4 * sizeof(int16_t);
    offset = ALIGN8(offset);

    auto size = TextureUtils::CalculateTextureSize(sTextureFormats.at(format), width, height);

    YAML::Node texture;
    texture["type"] = "TEXTURE";
    texture["offset"] = offset;
    texture["format"] = format;
    if (format == "CI4" || format == "CI8") {
        texture["tlut_symbol"] = symbol + "TLUT";
    }
    texture["ctype"] = "u16";
    texture["width"] = width;
    texture["height"] = height;
    texture["symbol"] = symbol + std::to_string(chunkNo);

    Companion::Instance->AddAsset(texture);
    offset += size;
}

ExportResult SpriteHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                          YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    return std::nullopt;
}

ExportResult SpriteCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                        YAML::Node& node, std::string* replacement) {
    auto sprite = std::static_pointer_cast<SpriteData>(raw);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "BKSpriteHeader " << symbol << "_Header = { " << sprite->mFrameCount << ", " << sprite->mFormatCode
          << " };\n\n";

    // Chunk count per frame
    if (!sprite->mChunkCounts.empty()) {
        write << "u16 " << symbol << "_ChunkCounts[] = {\n" << fourSpaceTab;
        for (size_t i = 0; i < sprite->mChunkCounts.size(); i++) {
            write << sprite->mChunkCounts[i];
            if (i < sprite->mChunkCounts.size() - 1) {
                write << ", ";
            }
        }
        write << "\n};\n\n";
    }

    // Chunk positions
    if (!sprite->mPositions.empty()) {
        write << "BKSpriteChunk " << symbol << "_Chunks[] = {\n";

        size_t chunkIndex = 0;
        for (size_t frameIdx = 0; frameIdx < sprite->mChunkCounts.size(); frameIdx++) {
            write << fourSpaceTab << "// Frame " << frameIdx << "\n";
            uint16_t chunkCount = sprite->mChunkCounts[frameIdx];

            for (uint16_t i = 0; i < chunkCount; i++) {
                if (chunkIndex < sprite->mPositions.size()) {
                    auto [x, y] = sprite->mPositions[chunkIndex];
                    write << fourSpaceTab << "{ " << x << ", " << y << " }";

                    // Tag the row with which texture it points at
                    write << ", // " << symbol << "_" << frameIdx << "_" << i;

                    if (chunkIndex < sprite->mPositions.size() - 1) {
                        write << ",";
                    }
                    write << "\n";
                    chunkIndex++;
                }
            }
        }
        write << "};\n\n";
    }

    return offset;
}

ExportResult SpriteBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                          YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto sprites = std::static_pointer_cast<SpriteData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKSprite, 0);

    auto wrapper = Companion::Instance->GetCurrentWrapper();

    writer.Write(sprites->mFormatCode);
    writer.Write(sprites->mUnk4);
    writer.Write(sprites->mUnk6);
    writer.Write(sprites->mUnk8);
    writer.Write(sprites->mUnkA);
    // Animation params unpacked from the ROM unkC bitfield
    writer.Write(sprites->mAnimSpeed);
    writer.Write(sprites->mAnimType);
    writer.Write(sprites->mAnimDirection);
    writer.Write(sprites->mAnimFlip);
    writer.Write((uint32_t)sprites->mPositions.size());
    for (auto position : sprites->mPositions) {
        writer.Write(position.first);
        writer.Write(position.second);
    }
    writer.Write((uint32_t)sprites->mChunkCounts.size());
    for (auto chunkCount : sprites->mChunkCounts) {
        writer.Write(chunkCount);
    }
    // Per-frame header data (x, y, w, h, unkA..unk12)
    writer.Write((uint32_t)sprites->mFrameHeaders.size());
    for (const auto& fh : sprites->mFrameHeaders) {
        writer.Write(fh.x);
        writer.Write(fh.y);
        writer.Write(fh.w);
        writer.Write(fh.h);
        writer.Write(fh.unkA);
        writer.Write(fh.unkC);
        writer.Write(fh.unkE);
        writer.Write(fh.unk10);
        writer.Write(fh.unk12);
    }

    writer.Finish(write);

    return std::nullopt;
}

ExportResult SpriteModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto sprite = std::static_pointer_cast<SpriteData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "FrameCount";
    out << YAML::Value << sprite->mFrameCount;
    out << YAML::Key << "FormatCode";
    out << YAML::Value << sprite->mFormatCode;
    out << YAML::Key << "Frames";
    out << YAML::Value;

    out << YAML::BeginSeq;
    size_t chunkIndex = 0;
    for (size_t frameIdx = 0; frameIdx < sprite->mChunkCounts.size(); frameIdx++) {
        out << YAML::BeginMap;
        out << YAML::Key << "ChunkCount";
        out << YAML::Value << sprite->mChunkCounts[frameIdx];
        out << YAML::Key << "Chunks";
        out << YAML::Value;

        out << YAML::BeginSeq;
        uint16_t chunkCount = sprite->mChunkCounts[frameIdx];
        for (uint16_t i = 0; i < chunkCount; i++) {
            if (chunkIndex < sprite->mPositions.size()) {
                auto [x, y] = sprite->mPositions[chunkIndex];
                out << YAML::Flow;
                out << YAML::BeginMap;
                out << YAML::Key << "X" << YAML::Value << x;
                out << YAML::Key << "Y" << YAML::Value << y;
                out << YAML::EndMap;
                chunkIndex++;
            }
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::EndMap;
    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SpriteFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto spriteOffset = GetSafeNode<uint32_t>(node, "offset"); // realistically always 0
    uint32_t offset;

    reader.SetEndianness(Torch::Endianness::Big);

    int16_t frameCount = reader.ReadInt16();
    int16_t formatCode = reader.ReadInt16();
    int16_t unk4 = reader.ReadInt16();
    int16_t unk6 = reader.ReadInt16();
    int16_t unk8 = reader.ReadInt16(); // display width, drives billboard vertex positioning
    int16_t unkA = reader.ReadInt16(); // display height, same deal
    // unkC packs the animation params into a BE u32 at offset 0x0C
    uint32_t unkC_raw = reader.ReadUInt32();
    uint8_t animSpeed = (unkC_raw >> 28) & 0xF;     // bits 31-28
    uint8_t animType = (unkC_raw >> 25) & 0x7;      // bits 27-25
    uint8_t animDirection = (unkC_raw >> 23) & 0x3; // bits 24-23
    uint8_t animFlip = (unkC_raw >> 21) & 0x3;      // bits 22-21
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
        case 0x80:
            format = "IA4";
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

    std::vector<uint16_t> chunkCounts;
    std::vector<std::pair<int16_t, int16_t>> positions;
    std::vector<SpriteFrameHeader> frameHeaders;

    if (frameCount > 0x100) {
        offset = spriteOffset + 8;
        std::string texSymbol = symbol + "_0_";
        ExtractChunk(reader, positions, offset, "RGBA16", texSymbol, 0);
        return std::make_shared<SpriteData>(frameCount, formatCode, chunkCounts, positions,
                                            std::vector<SpriteFrameHeader>{}, unk4, unk6, unk8, unkA, animSpeed,
                                            animType, animDirection, animFlip);
    }

    reader.Seek(0x10, LUS::SeekOffsetType::Start);
    std::vector<uint32_t> offsets;
    for (int16_t i = 0; i < frameCount; i++) {
        offsets.push_back(reader.ReadUInt32());
    }

    uint32_t frame = 0;
    for (const auto& frameOffset : offsets) {
        offset = spriteOffset + 0x10 + frameOffset + frameCount * sizeof(uint32_t);
        reader.Seek(offset - spriteOffset, LUS::SeekOffsetType::Start);
        int16_t x = reader.ReadInt16();
        int16_t y = reader.ReadInt16();
        int16_t width = reader.ReadInt16();
        int16_t height = reader.ReadInt16();
        uint16_t chunkCount = reader.ReadInt16();
        auto unkA = reader.ReadInt16();
        auto unkC = reader.ReadInt16();
        auto unkE = reader.ReadInt16();
        auto unk10 = reader.ReadInt16();
        auto unk12 = reader.ReadInt16();

        offset += 0x14;

        chunkCounts.push_back(chunkCount);
        frameHeaders.push_back({ x, y, width, height, unkA, unkC, unkE, unk10, unk12 });

        if (format == "CI4" || format == "CI8") {
            offset = ALIGN8(offset);

            int16_t colors = (format == "CI4") ? 0x10 : 0x100;
            YAML::Node tlut;
            tlut["type"] = "TEXTURE";
            tlut["offset"] = offset;
            tlut["format"] = "TLUT";
            tlut["ctype"] = "u16";
            tlut["colors"] = colors;
            tlut["symbol"] = symbol + "_" + std::to_string(frame) + "_TLUT";
            Companion::Instance->AddAsset(tlut);

            offset += colors * sizeof(int16_t);
        }

        for (uint16_t i = 0; i < chunkCount; i++) {
            std::string texSymbol = symbol + "_" + std::to_string(frame) + "_";
            ExtractChunk(reader, positions, offset, format, texSymbol, i);
        }
        frame++;
    }

    return std::make_shared<SpriteData>(frameCount, formatCode, chunkCounts, positions, frameHeaders, unk4, unk6, unk8,
                                        unkA, animSpeed, animType, animDirection, animFlip);
}

std::optional<std::shared_ptr<IParsedData>> SpriteFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                         YAML::Node& node) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string(reinterpret_cast<char*>(buffer.data()), buffer.size()));
    } catch (const YAML::ParserException& e) {
        SPDLOG_ERROR("Failed to parse sprite modding yaml: {}", e.what());
        return std::nullopt;
    }
    auto content = root.begin()->second;
    auto frames = content["Frames"];
    if (!frames || !frames.IsSequence() || frames.size() != 1) {
        // Only single-frame sprites (fonts, banners) support yaml-driven (re)build;
        // leave multi-frame sprites to the ROM parse.
        auto base = this->parse(Companion::Instance->GetRomData(), node);
        return base.has_value() ? std::optional(base.value()) : std::nullopt;
    }
    auto frame0 = frames[0];
    const auto newCount = GetSafeNode<uint16_t>(frame0, "ChunkCount");
    std::vector<std::pair<int16_t, int16_t>> positions;
    auto chunks = frame0["Chunks"];
    for (std::size_t i = 0; i < chunks.size(); i++) {
        YAML::Node chunk = chunks[i];
        positions.emplace_back(GetSafeNode<int16_t>(chunk, "X"), GetSafeNode<int16_t>(chunk, "Y"));
    }
    if (positions.empty() || positions.size() != newCount) {
        SPDLOG_ERROR("Sprite modding: ChunkCount {} != Chunks listed {}", newCount, positions.size());
        return std::nullopt;
    }

    const auto frameCount = GetSafeNode<int16_t>(content, "FrameCount", 1);
    const auto formatCode = GetSafeNode<int16_t>(content, "FormatCode", static_cast<int16_t>(0x100));

    // Header/anim/frame fields and the existing chunk count come from the ROM sprite — but
    // an *additive* sprite (an id the ROM lacks, e.g. a language-pack world-name banner at
    // 0x1600+) has no ROM sprite to read, so default them and supply every chunk from PNGs.
    int16_t unk4 = 0, unk6 = 0, unk8 = 0, unkA = 0;
    uint8_t animSpeed = 0, animType = 0, animDirection = 0, animFlip = 0;
    std::vector<SpriteFrameHeader> frameHeaders;
    uint16_t romCount = 0;
    const bool additive = node["additive"] && node["additive"].as<bool>();
    if (!additive) {
        auto base = this->parse(Companion::Instance->GetRomData(), node);
        if (!base.has_value()) {
            return std::nullopt;
        }
        auto sprite = std::static_pointer_cast<SpriteData>(base.value());
        unk4 = sprite->mUnk4;
        unk6 = sprite->mUnk6;
        unk8 = sprite->mUnk8;
        unkA = sprite->mUnkA;
        animSpeed = sprite->mAnimSpeed;
        animType = sprite->mAnimType;
        animDirection = sprite->mAnimDirection;
        animFlip = sprite->mAnimFlip;
        frameHeaders = sprite->mFrameHeaders;
        romCount = static_cast<uint16_t>(sprite->mPositions.size());
    } else {
        // Additive: no ROM sprite, so take the header/frame fields from the yaml (a
        // banner mirrors the JP layout); fields default to a frame sized to the chunk.
        const auto i16 = [](int v) { return static_cast<int16_t>(v); };
        unk4 = GetSafeNode<int16_t>(content, "Unk4", i16(0));
        unk6 = GetSafeNode<int16_t>(content, "Unk6", i16(0));
        unk8 = GetSafeNode<int16_t>(content, "Unk8", positions[0].first);
        unkA = GetSafeNode<int16_t>(content, "UnkA", positions[0].second);
        YAML::Node fh = frame0["FrameHeader"];
        if (fh) {
            frameHeaders.push_back({ GetSafeNode<int16_t>(fh, "x", i16(0)), GetSafeNode<int16_t>(fh, "y", i16(0)),
                                     GetSafeNode<int16_t>(fh, "w", positions[0].first),
                                     GetSafeNode<int16_t>(fh, "h", positions[0].second),
                                     GetSafeNode<int16_t>(fh, "unkA", i16(0)), GetSafeNode<int16_t>(fh, "unkC", i16(0)),
                                     GetSafeNode<int16_t>(fh, "unkE", i16(0)), GetSafeNode<int16_t>(fh, "unk10", i16(0)),
                                     GetSafeNode<int16_t>(fh, "unk12", i16(0)) });
        } else {
            frameHeaders.push_back({ 0, 0, positions[0].first, positions[0].second, 0, 0, 0, 0, 0 });
        }
    }

    std::string format;
    switch (formatCode) {
        case 0x1: format = "CI4"; break;
        case 0x4: format = "CI8"; break;
        case 0x20: format = "I4"; break;
        case 0x40: format = "I8"; break;
        case 0x80: format = "IA4"; break;
        case 0x100: format = "IA8"; break;
        case 0x400: format = "RGBA16"; break;
        case 0x800: format = "RGBA32"; break;
        default: format = "IA8"; break;
    }

    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    for (uint16_t i = romCount; i < newCount; i++) {
        YAML::Node texture;
        texture["type"] = "TEXTURE";
        texture["offset"] = 0xF0000000u + i;
        texture["format"] = format;
        texture["ctype"] = "u16";
        texture["width"] = additive ? frameHeaders[0].w : positions[i].first;
        texture["height"] = additive ? frameHeaders[0].h : positions[i].second;
        texture["symbol"] = symbol + "_0_" + std::to_string(i);
        Companion::Instance->AddAsset(texture);
    }

    return std::make_shared<SpriteData>(frameCount, formatCode, std::vector<uint16_t>{ newCount }, positions,
                                        frameHeaders, unk4, unk6, unk8, unkA, animSpeed, animType, animDirection,
                                        animFlip);
}

} // namespace BK64
