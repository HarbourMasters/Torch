#include "SpriteFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TextureUtils.h"
#include "spdlog/spdlog.h"
#include <sstream>
#include <unordered_set>

// PM64 sprite structure (decompressed, before byte-swap):
// 0x00: rastersOffset (u32)
// 0x04: palettesOffset (u32)
// 0x08: maxComponents (s32)
// 0x0C: colorVariations (s32)
// 0x10+: animListStart[] - variable length, -1 terminated

static void ByteSwapSpriteData(uint8_t* data, size_t size) {
    if (size < 16) {
        SPDLOG_WARN("Sprite data too small: {}", size);
        return;
    }

    // Byte-swap the header fields (first 4 u32s)
    uint32_t* header = reinterpret_cast<uint32_t*>(data);
    uint32_t rastersOffset = BSWAP32(header[0]);
    uint32_t palettesOffset = BSWAP32(header[1]);

    header[0] = rastersOffset;
    header[1] = palettesOffset;
    header[2] = BSWAP32(header[2]); // maxComponents
    header[3] = BSWAP32(header[3]); // colorVariations

    // Byte-swap animListStart array (starts at offset 0x10)
    // Each entry is a u32 offset, terminated by -1 (0xFFFFFFFF)
    uint32_t* animList = reinterpret_cast<uint32_t*>(data + 0x10);
    while (reinterpret_cast<uint8_t*>(animList) < data + size) {
        uint32_t val = BSWAP32(*animList);
        *animList = val;
        if (val == 0xFFFFFFFF) {
            break;
        }
        animList++;
    }

    // Byte-swap raster array entries
    // Each raster pointer entry is a u32 offset, terminated by -1
    if (rastersOffset > 0 && rastersOffset < size) {
        uint32_t* rasterList = reinterpret_cast<uint32_t*>(data + rastersOffset);
        while (reinterpret_cast<uint8_t*>(rasterList) < data + size) {
            uint32_t val = BSWAP32(*rasterList);
            *rasterList = val;
            if (val == 0xFFFFFFFF) {
                break;
            }

            // Each raster entry (SpriteRasterCacheEntry) at the pointed offset:
            // 0x00: image offset (u32)
            // 0x04: width (u8), height (u8), palette (s8), quadCacheIndex (s8)
            // Only the image offset (u32) needs byte-swap
            if (val > 0 && val < size - 4) {
                uint32_t* rasterEntry = reinterpret_cast<uint32_t*>(data + val);
                *rasterEntry = BSWAP32(*rasterEntry);
            }

            rasterList++;
        }
    }

    // Byte-swap palette array entries
    // Each palette pointer entry is a u32 offset, terminated by -1
    if (palettesOffset > 0 && palettesOffset < size) {
        uint32_t* paletteList = reinterpret_cast<uint32_t*>(data + palettesOffset);
        while (reinterpret_cast<uint8_t*>(paletteList) < data + size) {
            uint32_t val = BSWAP32(*paletteList);
            *paletteList = val;
            if (val == 0xFFFFFFFF) {
                break;
            }
            paletteList++;
        }
    }

    // Byte-swap animation component lists and commands
    // Walk through each animation in animListStart
    // IMPORTANT: PM64 sprites share data extensively - multiple animations can reference
    // the same component list, component structure, or command list. Track processed
    // offsets to prevent double-swapping (which would revert data to big-endian).
    std::unordered_set<uint32_t> processedAnimLists;
    std::unordered_set<uint32_t> processedComps;
    std::unordered_set<uint32_t> processedCmdLists;

    uint32_t* animListPtr = reinterpret_cast<uint32_t*>(data + 0x10);
    while (reinterpret_cast<uint8_t*>(animListPtr) < data + size) {
        uint32_t animOffset = *animListPtr;
        if (animOffset == 0xFFFFFFFF) {
            break;
        }

        if (animOffset > 0 && animOffset < size && !processedAnimLists.count(animOffset)) {
            processedAnimLists.insert(animOffset);

            // Each animation is a list of SpriteAnimComponent pointers, -1 terminated
            uint32_t* compList = reinterpret_cast<uint32_t*>(data + animOffset);
            while (reinterpret_cast<uint8_t*>(compList) < data + size) {
                uint32_t compOffset = BSWAP32(*compList);
                *compList = compOffset;
                if (compOffset == 0xFFFFFFFF) {
                    break;
                }

                if (compOffset > 0 && compOffset < size - 12 && !processedComps.count(compOffset)) {
                    processedComps.insert(compOffset);

                    // SpriteAnimComponent structure:
                    // 0x00: cmdList offset (u32)
                    // 0x04: cmdListSize (s16)
                    // 0x06: compOffset Vec3s (3 x s16)
                    uint32_t* compData = reinterpret_cast<uint32_t*>(data + compOffset);
                    uint32_t cmdListOffset = BSWAP32(compData[0]);
                    compData[0] = cmdListOffset;

                    uint16_t* compData16 = reinterpret_cast<uint16_t*>(data + compOffset + 4);
                    int16_t cmdListSize = static_cast<int16_t>(BSWAP16(compData16[0]));
                    compData16[0] = cmdListSize;
                    compData16[1] = BSWAP16(compData16[1]); // compOffset.x
                    compData16[2] = BSWAP16(compData16[2]); // compOffset.y
                    compData16[3] = BSWAP16(compData16[3]); // compOffset.z

                    // Byte-swap command list (array of u16)
                    if (cmdListOffset > 0 && cmdListOffset < size && cmdListSize > 0 &&
                        !processedCmdLists.count(cmdListOffset)) {
                        processedCmdLists.insert(cmdListOffset);

                        uint16_t* cmdList = reinterpret_cast<uint16_t*>(data + cmdListOffset);
                        int numCmds = cmdListSize / 2;
                        for (int i = 0; i < numCmds && reinterpret_cast<uint8_t*>(&cmdList[i]) < data + size; i++) {
                            cmdList[i] = BSWAP16(cmdList[i]);
                        }
                    }
                }
                compList++;
            }
        }
        animListPtr++;
    }

    // Palette pixel data (RGBA5551) is NOT byte-swapped.
    // The Fast3D interpreter reads palette bytes as big-endian:
    //   col16 = (palette[idx*2] << 8) | palette[idx*2+1]
    // so the raw ROM byte order must be preserved.
}

static uint32_t ReadBE32(const std::vector<uint8_t>& rom, size_t offset) {
    return (rom[offset] << 24) | (rom[offset + 1] << 16) | (rom[offset + 2] << 8) | rom[offset + 3];
}

// Walk the (already byte-swapped) blob's raster and palette lists. NPC raster
// images sit inside the blob at imageOffset; player sprites keep their images in
// the shared player raster data instead, located through the load descriptor
// table, so those are read from the ROM using the offsets the yaml provides.
static void CollectSpriteImages(PM64SpriteData& sprite, const std::vector<uint8_t>& rom, YAML::Node& node) {
    const auto& data = sprite.mBuffer;
    const size_t size = data.size();
    if (size < 16) {
        return;
    }
    const uint32_t rastersOffset = reinterpret_cast<const uint32_t*>(data.data())[0];
    const uint32_t palettesOffset = reinterpret_cast<const uint32_t*>(data.data())[1];

    std::vector<uint32_t> descriptors;
    uint32_t imageDataBase = 0, imageDataRom = 0;
    const bool player = node["raster_index"].IsDefined();
    if (player) {
        const auto index = GetSafeNode<uint32_t>(node, "raster_index");
        const auto header = GetSafeNode<uint32_t>(node, "raster_header");
        const auto sets = GetSafeNode<uint32_t>(node, "raster_sets");
        const auto descs = GetSafeNode<uint32_t>(node, "raster_descriptors");
        imageDataRom = GetSafeNode<uint32_t>(node, "raster_image_data");
        imageDataBase = ReadBE32(rom, header + 8);
        const uint32_t first = ReadBE32(rom, sets + index * 4);
        const uint32_t last = ReadBE32(rom, sets + (index + 1) * 4);
        for (uint32_t i = first; i < last; i++) {
            descriptors.push_back(ReadBE32(rom, descs + i * 4));
        }
    }

    if (rastersOffset > 0 && rastersOffset < size) {
        const uint32_t* list = reinterpret_cast<const uint32_t*>(data.data() + rastersOffset);
        for (size_t i = 0; list[i] != 0xFFFFFFFF && reinterpret_cast<const uint8_t*>(&list[i]) < data.data() + size;
             i++) {
            const uint32_t entry = list[i];
            if (entry == 0 || entry + 8 > size) {
                break;
            }
            PM64SpriteRaster raster;
            const uint32_t imageOffset = *reinterpret_cast<const uint32_t*>(data.data() + entry);
            raster.width = data[entry + 4];
            raster.height = data[entry + 5];
            const size_t bytes = (size_t)raster.width * raster.height / 2;
            if (player) {
                if (i >= descriptors.size()) {
                    break;
                }
                // upper three nibbles give size / 16, lower five the offset
                const uint32_t romOffset = imageDataRom + ((descriptors[i] & 0xFFFFF) - imageDataBase);
                const uint32_t descSize = (descriptors[i] >> 16) & 0xFFF0;
                // Placeholder entries (back-facing sprites, 255x255 dummies) point at a
                // 16-byte stub; leave those without pixels so nothing is emitted for them.
                if (romOffset + bytes <= rom.size() && descSize >= bytes) {
                    raster.pixels.assign(rom.begin() + romOffset, rom.begin() + romOffset + bytes);
                }
            } else if (imageOffset + bytes <= size) {
                raster.pixels.assign(data.begin() + imageOffset, data.begin() + imageOffset + bytes);
            }
            sprite.rasters.push_back(std::move(raster));
        }
    }

    if (palettesOffset > 0 && palettesOffset < size) {
        const uint32_t* list = reinterpret_cast<const uint32_t*>(data.data() + palettesOffset);
        for (size_t i = 0; list[i] != 0xFFFFFFFF && reinterpret_cast<const uint8_t*>(&list[i]) < data.data() + size;
             i++) {
            const uint32_t offset = list[i];
            if (offset + 32 > size) {
                break;
            }
            sprite.palettes.emplace_back(data.begin() + offset, data.begin() + offset + 32);
        }
    }
}

std::optional<std::shared_ptr<IParsedData>> PM64SpriteFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // Get the offset from YAML
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    // Check if this is compressed (YAY0)
    auto compressionType = Decompressor::GetCompressionType(buffer, offset);

    std::vector<uint8_t> spriteData;
    if (compressionType == CompressionType::YAY0) {
        // Decompress YAY0 data
        auto decoded = Decompressor::Decode(buffer, offset, CompressionType::YAY0);
        if (!decoded || decoded->size == 0) {
            SPDLOG_ERROR("Failed to decompress YAY0 sprite data at offset 0x{:X}", offset);
            return std::nullopt;
        }

        // Create a copy of decompressed data for byte-swapping
        spriteData.assign(decoded->data, decoded->data + decoded->size);

        SPDLOG_DEBUG("PM64:SPRITE parsed at 0x{:X}, decompressed size: {}", offset, spriteData.size());
    } else {
        // Uncompressed - just read raw data with size from YAML
        auto size = GetSafeNode<size_t>(node, "size");
        auto [_, segment] = Decompressor::AutoDecode(node, buffer, size);

        spriteData.assign(segment.data, segment.data + segment.size);
    }

    // Byte-swap for little-endian
    ByteSwapSpriteData(spriteData.data(), spriteData.size());

    auto sprite = std::make_shared<PM64SpriteData>(spriteData);
    CollectSpriteImages(*sprite, buffer, node);
    return sprite;
}

ExportResult PM64SpriteBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto sprite = std::static_pointer_cast<PM64SpriteData>(raw);
    auto& data = sprite->mBuffer;

    // Write as Blob type for now - game will load as raw binary
    WriteHeader(writer, Torch::ResourceType::Blob, 0);
    writer.Write(static_cast<uint32_t>(data.size()));
    writer.Write(reinterpret_cast<char*>(data.data()), data.size());
    writer.Finish(write);
    std::string base = entryName;
    auto lastSlash = base.rfind('/');
    if (lastSlash != std::string::npos) {
        base = base.substr(lastSlash + 1);
    }

    auto companion = [&](const std::string& name, uint32_t type, uint32_t w, uint32_t h,
                         const std::vector<uint8_t>& px) {
        auto texWriter = LUS::BinaryWriter();
        WriteHeader(texWriter, Torch::ResourceType::Texture, 0);
        texWriter.Write(type);
        texWriter.Write(w);
        texWriter.Write(h);
        texWriter.Write((uint32_t)px.size());
        texWriter.Write((char*)px.data(), px.size());
        std::stringstream ss;
        texWriter.Finish(ss);
        std::string str = ss.str();
        Companion::Instance->RegisterCompanionFile(name, std::vector<char>(str.begin(), str.end()));
    };

    for (size_t i = 0; i < sprite->rasters.size(); i++) {
        const auto& r = sprite->rasters[i];
        if (r.pixels.empty()) {
            continue;
        }
        companion(base + "_raster_" + std::to_string(i), (uint32_t)TextureType::Palette4bpp, r.width, r.height,
                  r.pixels);
    }
    for (size_t i = 0; i < sprite->palettes.size(); i++) {
        companion(base + "_pal_" + std::to_string(i), (uint32_t)TextureType::RGBA16bpp, 16, 1, sprite->palettes[i]);
    }

    return std::nullopt;
}

ExportResult PM64SpriteHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern u8 " << symbol << "[];\n";
    return std::nullopt;
}
