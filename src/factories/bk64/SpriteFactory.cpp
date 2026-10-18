#include "SpriteFactory.h"
#include "BKByteUtils.h"
#include "BKEmitText.h"
#include "BKResource.h"
#include "BKTexture.h"
#include "Companion.h"
#include "archive/SWrapper.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <fstream>
#include <iomanip>
#include <yaml-cpp/yaml.h>
extern "C" {
#include "n64graphics/n64graphics.h"
}

namespace BK64 {

#define ALIGN8(val) (((val) + 7) & ~7)

static bool ReadPngSize(const fs::path& path, int32_t& width, int32_t& height) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    uint8_t head[24];
    input.read(reinterpret_cast<char*>(head), sizeof(head));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(head))) {
        return false;
    }
    static const uint8_t kPngMagic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (std::memcmp(head, kPngMagic, sizeof(kPngMagic)) != 0 || std::memcmp(head + 12, "IHDR", 4) != 0) {
        return false;
    }
    width = static_cast<int32_t>(ReadU32BE(head + 16));
    height = static_cast<int32_t>(ReadU32BE(head + 20));
    return width > 0 && height > 0;
}

static bool FindModdedChunkSize(const std::string& chunkSymbol, int32_t& width, int32_t& height) {
    const auto& moddedPaths = Companion::Instance->GetModdedAssetPaths();
    for (const auto& [name, assetPath] : moddedPaths) {
        if (name.size() < chunkSymbol.size()) {
            continue;
        }
        const size_t at = name.size() - chunkSymbol.size();
        if (name.compare(at, chunkSymbol.size(), chunkSymbol) != 0 || (at > 0 && name[at - 1] != '/')) {
            continue;
        }
        const auto path = fs::path(Companion::Instance->GetConfig().moddingPath) / assetPath;
        return ReadPngSize(path, width, height);
    }
    return false;
}

void ExtractChunk(LUS::BinaryReader& reader, std::vector<std::pair<int16_t, int16_t>>& positions, uint32_t& offset,
                  std::string format, std::string symbol, uint32_t chunkNo, bool addAsset = true) {
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    int16_t x = reader.ReadInt16();
    int16_t y = reader.ReadInt16();
    int16_t width = reader.ReadInt16();
    int16_t height = reader.ReadInt16();

    positions.emplace_back(x, y);

    offset += 4 * sizeof(int16_t);
    offset = ALIGN8(offset);

    auto size = TextureUtils::CalculateTextureSize(FindTexelFormat(format)->texture, width, height);

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

    if (addAsset) {
        Companion::Instance->AddAsset(texture);
    }
    offset += size;
}

namespace {
const std::pair<int16_t, const char*> sSpriteFormats[] = {
    { 0x1, "CI4" },  { 0x4, "CI8" },   { 0x20, "I4" },      { 0x40, "I8" },
    { 0x80, "IA4" }, { 0x100, "IA8" }, { 0x400, "RGBA16" }, { 0x800, "RGBA32" },
};

const TexelFormat* spriteFormat(int16_t code) {
    for (const auto& [known, name] : sSpriteFormats) {
        if (known == code) {
            return FindTexelFormat(name);
        }
    }
    return nullptr;
}

int16_t spriteCode(const TexelFormat& format) {
    for (const auto& [code, name] : sSpriteFormats) {
        if (std::strcmp(format.name, name) == 0) {
            return code;
        }
    }
    return 0;
}

struct TexelGrid {
    int16_t originX = 0;
    int16_t originY = 0;
    int width = 0;
    int height = 0;
    std::vector<int64_t> values; // -1 where no chunk covers
    bool conflict = false;       // two chunks hold different texels for one pixel
};

TexelGrid frameTexels(const TexelFormat& format, const SpriteFrame& frame) {
    int minX = 0, minY = 0, maxX = frame.header.w, maxY = frame.header.h;
    for (const auto& chunk : frame.chunks) {
        minX = std::min<int>(minX, chunk.x);
        minY = std::min<int>(minY, chunk.y);
        maxX = std::max<int>(maxX, chunk.x + chunk.w);
        maxY = std::max<int>(maxY, chunk.y + chunk.h);
    }
    TexelGrid grid;
    grid.originX = (int16_t)minX;
    grid.originY = (int16_t)minY;
    grid.width = maxX - minX;
    grid.height = maxY - minY;
    grid.values.assign((size_t)grid.width * grid.height, -1);
    for (const auto& chunk : frame.chunks) {
        for (int i = 0; i < chunk.w * chunk.h; i++) {
            const size_t at = (size_t)(chunk.y - minY + i / chunk.w) * grid.width + (chunk.x - minX + i % chunk.w);
            const int64_t value = ReadTexel(format, chunk.texels, i);
            grid.conflict |= grid.values[at] >= 0 && grid.values[at] != value;
            grid.values[at] = value;
        }
    }
    return grid;
}

void computeBounds(const TexelFormat& format, SpriteFrame& frame) {
    int minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
    for (const auto& chunk : frame.chunks) {
        for (int i = 0; i < chunk.w * chunk.h; i++) {
            if (OpaqueTexel(format, ReadTexel(format, chunk.texels, i), frame.tlut)) {
                minX = std::min(minX, chunk.x + i % chunk.w);
                minY = std::min(minY, chunk.y + i / chunk.w);
                maxX = std::max(maxX, chunk.x + i % chunk.w);
                maxY = std::max(maxY, chunk.y + i / chunk.w);
            }
        }
    }
    auto& header = frame.header;
    if (minX > maxX) {
        header.unkC = header.unkE = header.unk10 = header.unk12 = 0;
        return;
    }
    // Measured from the pivot. func_80331D20 reads them.
    header.unkC = (int16_t)(minX - header.x);
    header.unkE = (int16_t)(minY - header.y);
    header.unk10 = (int16_t)(maxX - header.x);
    header.unk12 = (int16_t)(maxY - header.y);
}

// Strips that each fit TMEM, the half a palette leaves for CI, overlapping a row so filtering leaves no seam
std::vector<std::array<int, 4>> tmemStrips(const TexelFormat& format, int width, int height) {
    const size_t limit = IsIndexed(format) ? 2048 : 4096;
    int tileWidth = width;
    if (TexelBytes(format, width, 1) > limit) {
        tileWidth = (int)(limit * 8 / format.bits) & ~1;
    }
    // The port splits any chunk over 4096 texels again
    const int rows = std::max<int>(1, std::min((int)(limit / std::max<size_t>(1, TexelBytes(format, tileWidth, 1))),
                                               4096 / std::max(tileWidth, 1)));
    std::vector<std::array<int, 4>> strips;
    for (int y = 0; y < height;) {
        const int h = std::min(rows, height - y);
        for (int x = 0; x < width; x += tileWidth) {
            strips.push_back({ x, y, std::min(tileWidth, width - x), h });
        }
        if (y + h >= height) {
            break;
        }
        y += rows > 1 ? h - 1 : h;
    }
    return strips;
}

SpriteChunk cutChunk(const TexelFormat& format, const std::vector<uint32_t>& grid, int gridWidth, int gridHeight,
                     int originX, int originY, int x, int y, int w, int h) {
    SpriteChunk chunk{ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, {} };
    chunk.texels.assign(TexelBytes(format, w, h), 0);
    for (int i = 0; i < w * h; i++) {
        const int gx = x - originX + i % w;
        const int gy = y - originY + i / w;
        if (gx >= 0 && gy >= 0 && gx < gridWidth && gy < gridHeight) {
            WriteTexel(format, chunk.texels, i, grid[(size_t)gy * gridWidth + gx]);
        }
    }
    return chunk;
}

void setFrameArt(const TexelFormat& format, SpriteFrame& frame, RgbaImage image, const IndexedImage* source,
                 bool oneChunk) {
    auto& header = frame.header;
    const auto scale = [](int value, int from, int to) {
        return from > 0 ? (int16_t)std::lround((double)value * to / from) : (int16_t)(to / 2);
    };
    header.x = scale(header.x, header.w, image.width);
    header.y = scale(header.y, header.h, image.height);
    header.w = (int16_t)image.width;
    header.h = (int16_t)image.height;
    if (format.bits == 4 && image.width % 2) {
        // A 4-bit row has to end on a byte, so the art gets a clear column
        RgbaImage padded{ image.width + 1, image.height, {} };
        padded.pixels.assign((size_t)padded.width * padded.height * 4, 0);
        for (int y = 0; y < image.height; y++) {
            std::copy_n(&image.pixels[(size_t)y * image.width * 4], image.width * 4,
                        &padded.pixels[(size_t)y * padded.width * 4]);
        }
        image = std::move(padded);
        source = nullptr;
    }
    const auto grid = ImageTexels(format, image, source, frame.tlut);
    frame.chunks.clear();
    if (oneChunk) {
        frame.chunks.push_back(
            cutChunk(format, grid, image.width, image.height, 0, 0, 0, 0, image.width, image.height));
    } else {
        for (const auto& strip : tmemStrips(format, image.width, image.height)) {
            frame.chunks.push_back(
                cutChunk(format, grid, image.width, image.height, 0, 0, strip[0], strip[1], strip[2], strip[3]));
        }
    }
    computeBounds(format, frame);
}

std::vector<char> spriteResource(const SpriteData& sprite) {
    auto writer = LUS::BinaryWriter();
    BaseExporter::WriteHeader(writer, Torch::ResourceType::BKSprite, 0);

    writer.Write(sprite.mFormatCode);
    writer.Write(sprite.mUnk4);
    writer.Write(sprite.mUnk6);
    writer.Write(sprite.mUnk8);
    writer.Write(sprite.mUnkA);
    // Animation params unpacked from the ROM unkC bitfield
    writer.Write(sprite.mAnimSpeed);
    writer.Write(sprite.mAnimType);
    writer.Write(sprite.mAnimDirection);
    writer.Write(sprite.mAnimFlip);
    writer.Write((uint32_t)sprite.mPositions.size());
    for (auto position : sprite.mPositions) {
        writer.Write(position.first);
        writer.Write(position.second);
    }
    writer.Write((uint32_t)sprite.mChunkCounts.size());
    for (auto chunkCount : sprite.mChunkCounts) {
        writer.Write(chunkCount);
    }
    // Per-frame header data (x, y, w, h, unkA..unk12)
    writer.Write((uint32_t)sprite.mFrameHeaders.size());
    for (const auto& fh : sprite.mFrameHeaders) {
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
    return writer.ToVector();
}

} // namespace

const char* SpriteFormatName(int16_t code) {
    const TexelFormat* format = spriteFormat(code);
    return format != nullptr ? format->name : nullptr;
}

void LayoutSprite(SpriteData& sprite) {
    sprite.mFrameCount = (int16_t)sprite.mFrames.size();
    sprite.mChunkCounts.clear();
    sprite.mPositions.clear();
    sprite.mFrameHeaders.clear();
    for (const auto& frame : sprite.mFrames) {
        sprite.mChunkCounts.push_back((uint16_t)frame.chunks.size());
        for (const auto& chunk : frame.chunks) {
            sprite.mPositions.emplace_back(chunk.x, chunk.y);
        }
        sprite.mFrameHeaders.push_back(frame.header);
    }
}

std::shared_ptr<SpriteData> ReadSpriteRom(const std::vector<uint8_t>& bytes, std::string& error) {
    const auto need = [&](size_t at, size_t size) {
        if (at > bytes.size() || bytes.size() - at < size) {
            throw std::runtime_error("the sprite ends early");
        }
    };
    try {
        need(0, 0x10);
        const int16_t frameCount = ReadS16BE(&bytes[0]);
        const int16_t formatCode = ReadS16BE(&bytes[2]);
        if (frameCount > 0x100) {
            error = "it's a raw texture rather than a sprite";
            return nullptr;
        }
        const TexelFormat* format = spriteFormat(formatCode);
        if (format == nullptr) {
            error = "its format isn't one the game draws";
            return nullptr;
        }
        const uint32_t unkC = ReadU32BE(&bytes[12]);
        auto sprite = std::make_shared<SpriteData>(
            frameCount, formatCode, std::vector<uint16_t>(), std::vector<std::pair<int16_t, int16_t>>(),
            std::vector<SpriteFrameHeader>(), ReadS16BE(&bytes[4]), ReadS16BE(&bytes[6]), ReadS16BE(&bytes[8]),
            ReadS16BE(&bytes[10]), unkC >> 28 & 0xF, unkC >> 25 & 0x7, unkC >> 23 & 0x3, unkC >> 21 & 0x3);
        const size_t table = 0x10 + (size_t)std::max<int16_t>(frameCount, 0) * 4;
        need(0x10, table - 0x10);
        for (int16_t f = 0; f < frameCount; f++) {
            size_t at = table + ReadU32BE(&bytes[0x10 + f * 4]);
            need(at, 0x14);
            SpriteFrame frame;
            auto& header = frame.header;
            header.x = ReadS16BE(&bytes[at]);
            header.y = ReadS16BE(&bytes[at + 2]);
            header.w = ReadS16BE(&bytes[at + 4]);
            header.h = ReadS16BE(&bytes[at + 6]);
            const int16_t chunkCount = ReadS16BE(&bytes[at + 8]);
            header.unkA = ReadS16BE(&bytes[at + 10]);
            header.unkC = ReadS16BE(&bytes[at + 12]);
            header.unkE = ReadS16BE(&bytes[at + 14]);
            header.unk10 = ReadS16BE(&bytes[at + 16]);
            header.unk12 = ReadS16BE(&bytes[at + 18]);
            at += 0x14;
            if (IsIndexed(*format)) {
                at = ALIGN8(at);
                need(at, TlutColors(*format) * 2);
                frame.tlut.assign(bytes.begin() + at, bytes.begin() + at + TlutColors(*format) * 2);
                at += TlutColors(*format) * 2;
            }
            for (int16_t c = 0; c < chunkCount; c++) {
                need(at, 8);
                SpriteChunk chunk{ ReadS16BE(&bytes[at]),
                                   ReadS16BE(&bytes[at + 2]),
                                   ReadS16BE(&bytes[at + 4]),
                                   ReadS16BE(&bytes[at + 6]),
                                   {} };
                at = ALIGN8(at + 8);
                const size_t size = TexelBytes(*format, chunk.w, chunk.h);
                need(at, size);
                chunk.texels.assign(bytes.begin() + at, bytes.begin() + at + size);
                at += size;
                frame.chunks.push_back(std::move(chunk));
            }
            sprite->mFrames.push_back(std::move(frame));
        }
        LayoutSprite(*sprite);
        return sprite;
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

std::vector<uint8_t> WriteSpriteRom(const SpriteData& sprite) {
    const TexelFormat* format = spriteFormat(sprite.mFormatCode);
    if (format == nullptr) {
        return {};
    }
    std::vector<uint8_t> out;
    const auto put16 = [&](int16_t value) {
        out.push_back((uint8_t)((uint16_t)value >> 8));
        out.push_back((uint8_t)value);
    };
    const auto put32 = [&](uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            out.push_back((uint8_t)(value >> shift));
        }
    };
    // The game's own sprites pad with 0xAA
    const auto pad = [&] {
        while (out.size() % 8) {
            out.push_back(0xAA);
        }
    };

    put16((int16_t)sprite.mFrames.size());
    put16(sprite.mFormatCode);
    put16(sprite.mUnk4);
    put16(sprite.mUnk6);
    put16(sprite.mUnk8);
    put16(sprite.mUnkA);
    put32((uint32_t)(sprite.mAnimSpeed & 0xF) << 28 | (uint32_t)(sprite.mAnimType & 0x7) << 25 |
          (uint32_t)(sprite.mAnimDirection & 0x3) << 23 | (uint32_t)(sprite.mAnimFlip & 0x3) << 21);
    const size_t table = out.size();
    out.resize(table + sprite.mFrames.size() * 4);
    pad();
    const size_t base = table + sprite.mFrames.size() * 4;
    for (size_t f = 0; f < sprite.mFrames.size(); f++) {
        const auto& frame = sprite.mFrames[f];
        const uint32_t offset = (uint32_t)(out.size() - base);
        for (int b = 0; b < 4; b++) {
            out[table + f * 4 + b] = (uint8_t)(offset >> (24 - b * 8));
        }
        const auto& header = frame.header;
        for (int16_t value : { header.x, header.y, header.w, header.h, (int16_t)frame.chunks.size(), header.unkA,
                               header.unkC, header.unkE, header.unk10, header.unk12 }) {
            put16(value);
        }
        if (IsIndexed(*format)) {
            pad();
            std::vector<uint8_t> tlut = frame.tlut;
            tlut.resize(TlutColors(*format) * 2, 0);
            out.insert(out.end(), tlut.begin(), tlut.end());
        }
        for (const auto& chunk : frame.chunks) {
            for (int16_t value : { chunk.x, chunk.y, chunk.w, chunk.h }) {
                put16(value);
            }
            pad();
            std::vector<uint8_t> texels = chunk.texels;
            texels.resize(TexelBytes(*format, chunk.w, chunk.h), 0);
            out.insert(out.end(), texels.begin(), texels.end());
        }
    }
    return out;
}

std::shared_ptr<SpriteData> ReadSpriteResource(const std::vector<uint8_t>& resource, const SpriteFileReader& textures,
                                               std::string& error) {
    try {
        ResourceReader reader(resource);
        const int16_t formatCode = reader.Read<int16_t>();
        const int16_t unk4 = reader.Read<int16_t>();
        const int16_t unk6 = reader.Read<int16_t>();
        const int16_t unk8 = reader.Read<int16_t>();
        const int16_t unkA = reader.Read<int16_t>();
        uint8_t anim[4];
        for (auto& value : anim) {
            value = reader.Read<uint8_t>();
        }
        std::vector<std::pair<int16_t, int16_t>> positions(reader.Read<uint32_t>());
        for (auto& position : positions) {
            position.first = reader.Read<int16_t>();
            position.second = reader.Read<int16_t>();
        }
        std::vector<uint16_t> counts(reader.Read<uint32_t>());
        for (auto& count : counts) {
            count = reader.Read<uint16_t>();
        }
        std::vector<SpriteFrameHeader> headers(reader.Read<uint32_t>());
        for (auto& header : headers) {
            for (int16_t* field : { &header.x, &header.y, &header.w, &header.h, &header.unkA, &header.unkC,
                                    &header.unkE, &header.unk10, &header.unk12 }) {
                *field = reader.Read<int16_t>();
            }
        }
        if (counts.empty()) {
            error = "it's a raw texture rather than a sprite";
            return nullptr;
        }
        const TexelFormat* format = spriteFormat(formatCode);
        if (format == nullptr) {
            error = "its format isn't one the game draws";
            return nullptr;
        }

        auto sprite = std::make_shared<SpriteData>((int16_t)counts.size(), formatCode, counts, positions, headers, unk4,
                                                   unk6, unk8, unkA, anim[0], anim[1], anim[2], anim[3]);
        size_t position = 0;
        for (size_t f = 0; f < counts.size(); f++) {
            SpriteFrame frame{};
            if (f < headers.size()) {
                frame.header = headers[f];
            }
            const std::string prefix = "_" + std::to_string(f) + "_";
            std::vector<uint8_t> bytes;
            uint32_t width, height;
            const TexelFormat* stored;
            if (IsIndexed(*format)) {
                if (!textures(prefix + "TLUT", bytes)) {
                    error = "its " + prefix + "TLUT is missing";
                    return nullptr;
                }
                ReadTextureResource(bytes, stored, width, height, frame.tlut);
            }
            for (uint16_t c = 0; c < counts[f]; c++, position++) {
                if (!textures(prefix + std::to_string(c), bytes)) {
                    error = "its " + prefix + std::to_string(c) + " is missing";
                    return nullptr;
                }
                SpriteChunk chunk{};
                ReadTextureResource(bytes, stored, width, height, chunk.texels);
                if (position < positions.size()) {
                    chunk.x = positions[position].first;
                    chunk.y = positions[position].second;
                }
                chunk.w = (int16_t)width;
                chunk.h = (int16_t)height;
                frame.chunks.push_back(std::move(chunk));
            }
            sprite->mFrames.push_back(std::move(frame));
        }
        return sprite;
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

std::vector<SpriteFile> WriteSpriteResource(const SpriteData& sprite) {
    SpriteData laidOut = sprite;
    if (!laidOut.mFrames.empty()) {
        LayoutSprite(laidOut);
    }
    const auto bytes = spriteResource(laidOut);
    std::vector<SpriteFile> files{ { "", std::vector<uint8_t>(bytes.begin(), bytes.end()) } };
    const TexelFormat* format = spriteFormat(sprite.mFormatCode);
    if (format == nullptr) {
        return files;
    }
    for (size_t f = 0; f < sprite.mFrames.size(); f++) {
        const auto& frame = sprite.mFrames[f];
        const std::string prefix = "_" + std::to_string(f) + "_";
        if (IsIndexed(*format)) {
            std::vector<uint8_t> tlut = frame.tlut;
            tlut.resize(TlutColors(*format) * 2, 0);
            files.push_back({ prefix + "TLUT", TlutResource(tlut) });
        }
        for (size_t c = 0; c < frame.chunks.size(); c++) {
            const auto& chunk = frame.chunks[c];
            files.push_back({ prefix + std::to_string(c), TextureResource(*format, chunk.w, chunk.h, chunk.texels) });
        }
    }
    return files;
}

std::string SpriteToYaml(const SpriteData& sprite, const std::string& symbol, std::vector<SpriteFile>& images) {
    images.clear();
    const TexelFormat* format = spriteFormat(sprite.mFormatCode);
    if (format == nullptr) {
        return "";
    }
    const std::string stem = fs::path(symbol).filename().string();

    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << symbol << YAML::Value;
    out.SetIndent(2);
    out << YAML::BeginMap;
    out << YAML::Key << "Format" << YAML::Value << format->name;
    EmitList(out, "DisplaySize", { sprite.mUnk8, sprite.mUnkA });
    if (sprite.mUnk4 != 0) {
        out << YAML::Key << "Unk4" << YAML::Value << (int)sprite.mUnk4;
    }
    if (sprite.mUnk6 != 0) {
        out << YAML::Key << "Unk6" << YAML::Value << (int)sprite.mUnk6;
    }
    if (sprite.mAnimSpeed || sprite.mAnimType || sprite.mAnimDirection || sprite.mAnimFlip) {
        out << YAML::Key << "Animation" << YAML::Value << YAML::Flow << YAML::BeginMap;
        out << YAML::Key << "Speed" << YAML::Value << (int)sprite.mAnimSpeed;
        out << YAML::Key << "Type" << YAML::Value << (int)sprite.mAnimType;
        out << YAML::Key << "Direction" << YAML::Value << (int)sprite.mAnimDirection;
        out << YAML::Key << "Flip" << YAML::Value << (int)sprite.mAnimFlip;
        out << YAML::EndMap;
    }
    out << YAML::Key << "Frames" << YAML::Value << YAML::BeginSeq;
    for (size_t f = 0; f < sprite.mFrames.size(); f++) {
        const auto& frame = sprite.mFrames[f];
        const auto& header = frame.header;
        const auto grid = frameTexels(*format, frame);
        const std::string prefix = stem + "_" + std::to_string(f);
        out << YAML::BeginMap;
        if (!grid.conflict) {
            images.push_back({ prefix + ".png", TexelsPng(*format, grid.values, grid.width, grid.height, frame.tlut) });
            out << YAML::Key << "Image" << YAML::Value << prefix + ".png";
        }
        EmitList(out, "Size", { header.w, header.h });
        EmitList(out, "Pivot", { header.x, header.y });
        EmitList(out, "Bounds", { header.unkC, header.unkE, header.unk10, header.unk12 });
        if (header.unkA != 0) {
            out << YAML::Key << "UnkA" << YAML::Value << (int)header.unkA;
        }
        if (!grid.conflict) {
            out << YAML::Key << "Chunks" << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (const auto& chunk : frame.chunks) {
                out << YAML::Flow << YAML::BeginSeq << chunk.x << chunk.y << chunk.w << chunk.h << YAML::EndSeq;
            }
            out << YAML::EndSeq;
        } else {
            out << YAML::Key << "Chunks" << YAML::Value << YAML::BeginSeq;
            for (size_t c = 0; c < frame.chunks.size(); c++) {
                const auto& chunk = frame.chunks[c];
                std::vector<int64_t> values(chunk.w * chunk.h);
                for (size_t i = 0; i < values.size(); i++) {
                    values[i] = ReadTexel(*format, chunk.texels, i);
                }
                const std::string name = prefix + "_" + std::to_string(c) + ".png";
                images.push_back({ name, TexelsPng(*format, values, chunk.w, chunk.h, frame.tlut) });
                out << YAML::Flow << YAML::BeginMap;
                EmitList(out, "Position", { chunk.x, chunk.y });
                out << YAML::Key << "Image" << YAML::Value << name << YAML::EndMap;
            }
            out << YAML::EndSeq;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
    return std::string(out.c_str()) + "\n";
}

bool SpriteFromYaml(const std::string& text, const SpriteFileReader& files, SpriteData& out, std::string& error) {
    try {
        const YAML::Node root = YAML::Load(text);
        if (!root.IsMap() || root.size() != 1) {
            YamlFail("sprite", "needs one top-level key, the asset's name");
        }
        const YAML::Node sprite = root.begin()->second;
        YamlKeys(sprite, { "Format", "DisplaySize", "Unk4", "Unk6", "Animation", "Frames" }, "sprite");
        const TexelFormat* format = sprite["Format"] ? FindTexelFormat(sprite["Format"].as<std::string>()) : nullptr;
        if (format == nullptr) {
            YamlFail("Format", "needs one of CI4, CI8, I4, I8, IA4, IA8, RGBA16 or RGBA32");
        }
        const YAML::Node frames = sprite["Frames"];
        if (!frames || !frames.IsSequence() || frames.size() == 0) {
            YamlFail("sprite", "needs Frames");
        }
        // func_80344A1C builds display lists for 200 frames at most
        if (frames.size() > 200) {
            YamlFail("Frames", "hold more than 200");
        }

        out = SpriteData(0, spriteCode(*format), {}, {});
        out.mUnk4 = sprite["Unk4"] ? YamlShort(sprite["Unk4"], "Unk4") : 0;
        out.mUnk6 = sprite["Unk6"] ? YamlShort(sprite["Unk6"], "Unk6") : 0;
        if (const YAML::Node animation = sprite["Animation"]) {
            YamlKeys(animation, { "Speed", "Type", "Direction", "Flip" }, "Animation");
            const auto field = [&](const char* key, uint32_t max) {
                return animation[key] ? (uint8_t)YamlNumber(animation[key], std::string("Animation ") + key, max) : 0;
            };
            out.mAnimSpeed = field("Speed", 15);
            out.mAnimType = field("Type", 7);
            out.mAnimDirection = field("Direction", 3);
            out.mAnimFlip = field("Flip", 3);
        }

        std::map<std::string, std::vector<RgbaImage>> gifs;
        const auto load = [&](const YAML::Node& value, size_t frameIndex, RgbaImage& rgba, IndexedImage& indexed,
                              bool& hasIndexed, const std::string& where) {
            std::string name = value.as<std::string>();
            size_t pick = frameIndex;
            // walk.gif#3 takes the GIF's frame 3. Without it, a frame takes the one with its own number.
            if (const size_t hash = name.rfind('#'); hash != std::string::npos) {
                pick = YamlNumber(YAML::Node(name.substr(hash + 1)), where, 0xFFFF);
                name = name.substr(0, hash);
            }
            std::vector<uint8_t> bytes;
            if (!files(name, bytes)) {
                YamlFail(where, "can't read " + name);
            }
            hasIndexed = false;
            std::string problem;
            if (IsGif(bytes)) {
                auto& decoded = gifs[name];
                if (decoded.empty() && !DecodeImage(bytes, decoded, nullptr, problem)) {
                    YamlFail(where, name + ": " + problem);
                }
                if (pick >= decoded.size()) {
                    YamlFail(where, name + " has " + std::to_string(decoded.size()) + " frames");
                }
                rgba = decoded[pick];
                return;
            }
            std::vector<RgbaImage> decoded;
            if (!DecodeImage(bytes, decoded, nullptr, problem)) {
                YamlFail(where, name + ": " + problem);
            }
            rgba = std::move(decoded[0]);
            hasIndexed = DecodeIndexedPng(bytes, indexed);
        };

        for (size_t f = 0; f < frames.size(); f++) {
            const YAML::Node entry = frames[f];
            const std::string where = "Frames " + std::to_string(f);
            YamlKeys(entry, { "Image", "Size", "Pivot", "Bounds", "UnkA", "Chunks" }, where);
            SpriteFrame frame{};
            auto& header = frame.header;
            if (entry["Size"]) {
                int16_t size[2];
                ReadList(entry, "Size", size, 2, where);
                header.w = size[0];
                header.h = size[1];
            }
            if (entry["Pivot"]) {
                int16_t pivot[2];
                ReadList(entry, "Pivot", pivot, 2, where);
                header.x = pivot[0];
                header.y = pivot[1];
            }
            header.unkA = entry["UnkA"] ? YamlShort(entry, "UnkA", where) : 0;
            const YAML::Node chunks = entry["Chunks"];
            if (chunks && !chunks.IsSequence()) {
                YamlFail(where + " Chunks", "needs to be a list");
            }
            const auto readBounds = [&] {
                if (entry["Bounds"]) {
                    int16_t bounds[4];
                    ReadList(entry, "Bounds", bounds, 4, where);
                    header.unkC = bounds[0];
                    header.unkE = bounds[1];
                    header.unk10 = bounds[2];
                    header.unk12 = bounds[3];
                } else {
                    computeBounds(*format, frame);
                }
            };

            if (entry["Image"]) {
                RgbaImage rgba;
                IndexedImage indexed;
                bool hasIndexed = false;
                load(entry["Image"], f, rgba, indexed, hasIndexed, where + " Image");
                if (rgba.width > INT16_MAX || rgba.height > INT16_MAX) {
                    YamlFail(where + " Image", "is too big");
                }
                std::vector<std::array<int, 4>> rects;
                int minX = 0, minY = 0, maxX = header.w, maxY = header.h;
                for (size_t c = 0; chunks && c < chunks.size(); c++) {
                    int16_t rect[4];
                    ReadList(chunks[c], rect, 4, where + " Chunks " + std::to_string(c));
                    rects.push_back({ rect[0], rect[1], rect[2], rect[3] });
                    minX = std::min<int>(minX, rect[0]);
                    minY = std::min<int>(minY, rect[1]);
                    maxX = std::max<int>(maxX, rect[0] + rect[2]);
                    maxY = std::max<int>(maxY, rect[1] + rect[3]);
                }
                if (!rects.empty() && entry["Size"] && rgba.width == maxX - minX && rgba.height == maxY - minY) {
                    // Same canvas as the export, so the chunks are cut where they were
                    const auto grid = ImageTexels(*format, rgba, hasIndexed ? &indexed : nullptr, frame.tlut);
                    for (const auto& rect : rects) {
                        frame.chunks.push_back(cutChunk(*format, grid, rgba.width, rgba.height, minX, minY, rect[0],
                                                        rect[1], rect[2], rect[3]));
                    }
                    readBounds();
                } else {
                    // New art: the pivot scales from Size, and the chunks and bounds are redone
                    if (!entry["Size"]) {
                        header.w = (int16_t)rgba.width;
                        header.h = (int16_t)rgba.height;
                    }
                    if (!entry["Pivot"]) {
                        header.x = header.w / 2;
                        header.y = header.h / 2;
                    }
                    setFrameArt(*format, frame, rgba, hasIndexed ? &indexed : nullptr, rects.size() <= 1);
                }
            } else {
                if (!entry["Size"]) {
                    YamlFail(where, "needs an Image, or a Size and Chunks with images");
                }
                if (!chunks || chunks.size() == 0) {
                    YamlFail(where, "needs an Image, or Chunks with images");
                }
                for (size_t c = 0; c < chunks.size(); c++) {
                    const std::string at = where + " Chunks " + std::to_string(c);
                    YamlKeys(chunks[c], { "Position", "Image" }, at);
                    if (!chunks[c]["Position"] || !chunks[c]["Image"]) {
                        YamlFail(at, "needs a Position and an Image");
                    }
                    int16_t position[2];
                    ReadList(chunks[c], "Position", position, 2, at);
                    RgbaImage rgba;
                    IndexedImage indexed;
                    bool hasIndexed = false;
                    load(chunks[c]["Image"], f, rgba, indexed, hasIndexed, at + " Image");
                    std::vector<uint8_t> tlut;
                    const auto grid = ImageTexels(*format, rgba, hasIndexed ? &indexed : nullptr, tlut);
                    if (c == 0) {
                        frame.tlut = tlut;
                    } else if (tlut != frame.tlut) {
                        YamlFail(at + " Image", "needs the same palette as the frame's first chunk");
                    }
                    frame.chunks.push_back(cutChunk(*format, grid, rgba.width, rgba.height, position[0], position[1],
                                                    position[0], position[1], rgba.width, rgba.height));
                }
                readBounds();
            }
            out.mFrames.push_back(std::move(frame));
        }

        if (sprite["DisplaySize"]) {
            int16_t size[2];
            ReadList(sprite["DisplaySize"], size, 2, "DisplaySize");
            out.mUnk8 = size[0];
            out.mUnkA = size[1];
        } else {
            out.mUnk8 = out.mFrames[0].header.w;
            out.mUnkA = out.mFrames[0].header.h;
        }
        LayoutSprite(out);
        out.mWriteTextures = true;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

SpriteCanvas DrawSpriteFrame(const SpriteData& sprite, size_t frame) {
    SpriteCanvas canvas;
    const TexelFormat* format = spriteFormat(sprite.mFormatCode);
    if (format == nullptr || frame >= sprite.mFrames.size()) {
        return canvas;
    }
    const auto grid = frameTexels(*format, sprite.mFrames[frame]);
    canvas.originX = grid.originX;
    canvas.originY = grid.originY;
    canvas.image.width = grid.width;
    canvas.image.height = grid.height;
    canvas.image.pixels.assign((size_t)grid.width * grid.height * 4, 0);
    for (size_t i = 0; i < grid.values.size(); i++) {
        if (grid.values[i] >= 0) {
            TexelRgba(*format, (uint32_t)grid.values[i], sprite.mFrames[frame].tlut, &canvas.image.pixels[i * 4]);
        }
    }
    return canvas;
}

void SetSpriteFrameImage(SpriteData& sprite, size_t frame, const RgbaImage& image, bool oneChunk) {
    const TexelFormat* format = spriteFormat(sprite.mFormatCode);
    if (format == nullptr || frame >= sprite.mFrames.size()) {
        return;
    }
    setFrameArt(*format, sprite.mFrames[frame], image, nullptr, oneChunk);
    LayoutSprite(sprite);
}

bool RedrawSpriteFrame(SpriteData& sprite, size_t frame, const RgbaImage& canvas) {
    const TexelFormat* format = spriteFormat(sprite.mFormatCode);
    if (format == nullptr || frame >= sprite.mFrames.size()) {
        return false;
    }
    SpriteFrame& target = sprite.mFrames[frame];
    const auto grid = frameTexels(*format, target);
    if (canvas.width != grid.width || canvas.height != grid.height) {
        return false;
    }
    const auto texels = ImageTexels(*format, canvas, nullptr, target.tlut);
    for (auto& chunk : target.chunks) {
        chunk = cutChunk(*format, texels, grid.width, grid.height, grid.originX, grid.originY, chunk.x, chunk.y,
                         chunk.w, chunk.h);
    }
    computeBounds(*format, target);
    LayoutSprite(sprite);
    return true;
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
    auto sprites = std::static_pointer_cast<SpriteData>(raw);
    const auto bytes = spriteResource(*sprites);
    write.write(bytes.data(), bytes.size());

    if (sprites->mWriteTextures) {
        // RegisterCompanionFile would put them under the sprite's own folder
        for (const auto& file : WriteSpriteResource(*sprites)) {
            if (!file.name.empty()) {
                Companion::Instance->RegisterArchiveFile(entryName + file.name,
                                                         std::vector<char>(file.bytes.begin(), file.bytes.end()));
            }
        }
    }
    return std::nullopt;
}

ExportResult SpriteModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto sprite = std::static_pointer_cast<SpriteData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (!sprite->mFrames.empty()) {
        std::vector<SpriteFile> images;
        const std::string yaml = SpriteToYaml(*sprite, symbol, images);
        const auto folder = fs::path(symbol).parent_path();
        for (const auto& image : images) {
            Companion::Instance->RegisterCompanionFile((folder / image.name).generic_string(),
                                                       std::vector<char>(image.bytes.begin(), image.bytes.end()));
        }
        *replacement += ".yaml";
        write.write(yaml.data(), yaml.size());
        return std::nullopt;
    }

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

    std::string error;
    const auto texels = ReadSpriteRom(std::vector<uint8_t>(segment.data, segment.data + segment.size), error);
    // A modding export writes whole frames, so the chunks aren't exported on their own
    const bool addTextures = texels == nullptr || Companion::Instance->GetConfig().exporterType != ExportType::Modding;

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
            if (addTextures) {
                Companion::Instance->AddAsset(tlut);
            }

            offset += colors * sizeof(int16_t);
        }

        for (uint16_t i = 0; i < chunkCount; i++) {
            std::string texSymbol = symbol + "_" + std::to_string(frame) + "_";
            ExtractChunk(reader, positions, offset, format, texSymbol, i, addTextures);
        }
        frame++;
    }

    auto sprite = std::make_shared<SpriteData>(frameCount, formatCode, chunkCounts, positions, frameHeaders, unk4, unk6,
                                               unk8, unkA, animSpeed, animType, animDirection, animFlip);
    if (texels != nullptr) {
        sprite->mFrames = texels->mFrames;
    }
    return sprite;
}

std::optional<std::shared_ptr<IParsedData>> SpriteFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                         YAML::Node& node) {
    auto content = YamlEntry(buffer, GetSafeNode<std::string>(node, "symbol"));
    if (content["Format"]) {
        const auto name = GetSafeNode<std::string>(node, "vpath", "");
        const auto& modded = Companion::Instance->GetModdedAssetPaths();
        const auto yaml = modded.find(name);
        const fs::path folder = fs::path(Companion::Instance->GetConfig().moddingPath) /
                                (yaml != modded.end() ? fs::path(yaml->second).parent_path()
                                                      : fs::path(Companion::Instance->GetCurrentDirectory()));
        const auto files = [&](const std::string& file, std::vector<uint8_t>& out) {
            std::ifstream in(folder / file, std::ios::binary);
            if (!in) {
                return false;
            }
            out.assign(std::istreambuf_iterator<char>(in), {});
            return true;
        };
        auto sprite =
            std::make_shared<SpriteData>(0, 0, std::vector<uint16_t>(), std::vector<std::pair<int16_t, int16_t>>());
        std::string error;
        if (!SpriteFromYaml(std::string(reinterpret_cast<char*>(buffer.data()), buffer.size()), files, *sprite,
                            error)) {
            throw std::runtime_error(fmt::format("Sprite modding: {}: {}", name, error));
        }
        return sprite;
    }
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
        throw std::runtime_error(
            fmt::format("Sprite modding: ChunkCount {} != Chunks listed {}", newCount, positions.size()));
    }

    const auto frameCount = GetSafeNode<int16_t>(content, "FrameCount", 1);
    const auto formatCode = GetSafeNode<int16_t>(content, "FormatCode", static_cast<int16_t>(0x100));
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    // Header/anim/frame fields and the existing chunk count come from the ROM sprite — but
    // an *additive* sprite (an id the ROM lacks, e.g. a language-pack world-name banner at
    // 0x1600+) has no ROM sprite to read, so default them and supply every chunk from PNGs.
    int16_t unk4 = 0, unk6 = 0, unk8 = 0, unkA = 0;
    uint8_t animSpeed = 0, animType = 0, animDirection = 0, animFlip = 0;
    std::vector<SpriteFrameHeader> frameHeaders;
    std::vector<std::pair<int32_t, int32_t>> chunkSizes;
    uint16_t romCount = 0;
    const bool additive = node["additive"] && node["additive"].as<bool>();
    if (!additive) {
        auto base = this->parse(Companion::Instance->GetRomData(), node);
        if (!base.has_value()) {
            throw std::runtime_error("Sprite modding: " + symbol + " isn't in the ROM to build on");
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
        // banner mirrors the JP layout). The frame's size is measured off the artwork.
        const auto i16 = [](int v) { return static_cast<int16_t>(v); };
        int32_t measuredW = 0, measuredH = 0;
        for (uint16_t i = 0; i < newCount; i++) {
            int32_t cw = 0, chh = 0;
            if (!FindModdedChunkSize(symbol + "_0_" + std::to_string(i), cw, chh)) {
                SPDLOG_WARN("Sprite modding: no PNG found for additive chunk {}_0_{}; frame size may be wrong", symbol,
                            i);
                cw = chh = 0;
            }
            chunkSizes.emplace_back(cw, chh);
            measuredW = std::max(measuredW, positions[i].first + cw);
            measuredH = std::max(measuredH, positions[i].second + chh);
        }
        if (measuredW <= 0 || measuredH <= 0) {
            throw std::runtime_error(
                fmt::format("Sprite modding: additive sprite {} has no readable chunk artwork", symbol));
        }
        unk4 = GetSafeNode<int16_t>(content, "Unk4", i16(0));
        unk6 = GetSafeNode<int16_t>(content, "Unk6", i16(0));
        unk8 = GetSafeNode<int16_t>(content, "Unk8", i16(measuredW));
        unkA = GetSafeNode<int16_t>(content, "UnkA", i16(measuredH));
        YAML::Node fh = frame0["FrameHeader"];
        if (fh) {
            frameHeaders.push_back(
                { GetSafeNode<int16_t>(fh, "x", i16(0)), GetSafeNode<int16_t>(fh, "y", i16(0)),
                  GetSafeNode<int16_t>(fh, "w", i16(measuredW)), GetSafeNode<int16_t>(fh, "h", i16(measuredH)),
                  GetSafeNode<int16_t>(fh, "unkA", i16(0)), GetSafeNode<int16_t>(fh, "unkC", i16(0)),
                  GetSafeNode<int16_t>(fh, "unkE", i16(0)), GetSafeNode<int16_t>(fh, "unk10", i16(0)),
                  GetSafeNode<int16_t>(fh, "unk12", i16(0)) });
        } else {
            frameHeaders.push_back({ 0, 0, i16(measuredW), i16(measuredH), 0, 0, 0, 0, 0 });
        }
    }

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
            format = "IA8";
            break;
    }

    for (uint16_t i = romCount; i < newCount; i++) {
        YAML::Node texture;
        texture["type"] = "TEXTURE";
        texture["offset"] = 0xF0000000u + i;
        texture["format"] = format;
        texture["ctype"] = "u16";
        int32_t width = additive ? chunkSizes[i].first : 0;
        int32_t height = additive ? chunkSizes[i].second : 0;
        if (!additive && !FindModdedChunkSize(symbol + "_0_" + std::to_string(i), width, height)) {
            SPDLOG_WARN("Sprite modding: no PNG found for added chunk {}_0_{}", symbol, i);
        }
        texture["width"] = width;
        texture["height"] = height;
        texture["symbol"] = symbol + "_0_" + std::to_string(i);
        Companion::Instance->AddAsset(texture);
    }

    return std::make_shared<SpriteData>(frameCount, formatCode, std::vector<uint16_t>{ newCount }, positions,
                                        frameHeaders, unk4, unk6, unk8, unkA, animSpeed, animType, animDirection,
                                        animFlip);
}

} // namespace BK64
