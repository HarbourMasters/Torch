#include "BackgroundFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"

// Each header is 16 bytes:
//   0x00: rasterAddr  (u32)
//   0x04: paletteAddr (u32)
//   0x08: startX      (u16)
//   0x0A: startY      (u16)
//   0x0C: width       (u16)
//   0x0E: height      (u16)
//
// Most backgrounds have 1 header + 1 palette + 1 raster (CI8).
// Some (e.g. sbk_bg) have multiple headers sharing the same raster
// but pointing to different palettes.

static constexpr uint32_t VRAM_BASE = 0x80200000;

static uint32_t ReadBE32(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint16_t ReadBE16(const uint8_t* p) {
    return (p[0] << 8) | p[1];
}

std::optional<std::shared_ptr<IParsedData>> PM64BackgroundFactory::parse(std::vector<uint8_t>& buffer,
                                                                         YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    std::vector<uint8_t> bgData;
    auto compressionType = Decompressor::GetCompressionType(buffer, offset);

    if (compressionType == CompressionType::YAY0) {
        auto decoded = Decompressor::Decode(buffer, offset, CompressionType::YAY0);
        if (!decoded || decoded->size == 0) {
            SPDLOG_ERROR("Failed to decompress background at 0x{:X}", offset);
            return std::nullopt;
        }
        bgData.assign(decoded->data, decoded->data + decoded->size);
    } else {
        auto size = GetSafeNode<size_t>(node, "size");
        auto [_, segment] = Decompressor::AutoDecode(node, buffer, size);
        bgData.assign(segment.data, segment.data + segment.size);
    }

    if (bgData.size() < 0x10) {
        SPDLOG_ERROR("Background data too small at 0x{:X}: {} bytes", offset, bgData.size());
        return std::nullopt;
    }

    uint8_t* data = bgData.data();
    size_t dataSize = bgData.size();

    uint32_t rasterOffset = ReadBE32(data + 0x00) - VRAM_BASE;
    uint32_t palette0Offset = ReadBE32(data + 0x04) - VRAM_BASE;
    uint16_t width = ReadBE16(data + 0x0C);
    uint16_t height = ReadBE16(data + 0x0E);
    uint32_t rasterSize = width * height;

    if (rasterOffset + rasterSize > dataSize) {
        SPDLOG_ERROR("Background raster out of bounds: 0x{:X}+0x{:X} > 0x{:X}", rasterOffset, rasterSize, dataSize);
        return std::nullopt;
    }
    if (palette0Offset + 512 > dataSize) {
        SPDLOG_ERROR("Background palette 0 out of bounds");
        return std::nullopt;
    }

    std::vector<uint8_t> raster(data + rasterOffset, data + rasterOffset + rasterSize);

    std::vector<std::vector<uint8_t>> palettes;
    palettes.emplace_back(data + palette0Offset, data + palette0Offset + 512);

    // Scan for additional headers (e.g. sbk_bg has 2 palettes sharing the same raster)
    for (uint32_t hdrOff = 0x10; hdrOff + 0x10 <= rasterOffset; hdrOff += 0x10) {
        uint32_t nextRasterAddr = ReadBE32(data + hdrOff);
        uint32_t nextPalAddr = ReadBE32(data + hdrOff + 4);

        if ((nextRasterAddr & 0xFFF00000) != 0x80200000 || (nextPalAddr & 0xFFF00000) != 0x80200000) {
            break;
        }

        uint32_t nextPalOff = nextPalAddr - VRAM_BASE;
        if (nextRasterAddr - VRAM_BASE != rasterOffset || nextPalOff == palette0Offset) {
            break;
        }
        if (nextPalOff + 512 > dataSize) {
            break;
        }

        palettes.emplace_back(data + nextPalOff, data + nextPalOff + 512);
    }

    SPDLOG_INFO("Background: {}x{} palettes={}", width, height, palettes.size());

    return std::make_shared<PM64BackgroundData>(width, height, ReadBE16(data + 0x08), ReadBE16(data + 0x0A),
                                                std::move(raster), std::move(palettes));
}

ExportResult PM64BackgroundBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                  std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto bg = std::static_pointer_cast<PM64BackgroundData>(raw);

    // Raster as Texture (CI8)
    auto writer = LUS::BinaryWriter();
    WriteHeader(writer, Torch::ResourceType::Texture, 0);
    writer.Write((uint32_t)TextureType::Palette8bpp);
    writer.Write((uint32_t)bg->width);
    writer.Write((uint32_t)bg->height);
    writer.Write((uint32_t)bg->raster.size());
    writer.Write((char*)bg->raster.data(), bg->raster.size());
    writer.Finish(write);

    // Each palette as a companion Texture (RGBA16, 256×1)
    for (uint32_t i = 0; i < bg->palettes.size(); i++) {
        auto palWriter = LUS::BinaryWriter();
        WriteHeader(palWriter, Torch::ResourceType::Texture, 0);
        palWriter.Write((uint32_t)TextureType::RGBA16bpp);
        palWriter.Write((uint32_t)256);
        palWriter.Write((uint32_t)1);
        palWriter.Write((uint32_t)bg->palettes[i].size());
        palWriter.Write((char*)bg->palettes[i].data(), bg->palettes[i].size());

        std::stringstream ss;
        palWriter.Finish(ss);
        std::string str = ss.str();
        std::vector<char> palData(str.begin(), str.end());

        std::string palPath = entryName + "_pal" + std::to_string(i);
        auto lastSlash = palPath.rfind('/');
        if (lastSlash != std::string::npos) {
            palPath = palPath.substr(lastSlash + 1);
        }

        Companion::Instance->RegisterCompanionFile(palPath, palData);
    }

    return std::nullopt;
}

ExportResult PM64BackgroundHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                  std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto bg = std::static_pointer_cast<PM64BackgroundData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n";

        for (uint32_t i = 0; i < bg->palettes.size(); i++) {
            std::string palEntryName = (*replacement) + "_pal" + std::to_string(i);
            write << "static const ALIGN_ASSET(2) char " << symbol << "_pal" << i << "[] = \"__OTR__" << palEntryName
                  << "\";\n";
        }

        write << "\n";
        return std::nullopt;
    }

    write << "extern u8 " << symbol << "[];\n";
    return std::nullopt;
}
