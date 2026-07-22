#include "CollisionFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"

// PM64 collision file structure (N64 ROM format):
//
// HitFile header (8 bytes):
//   0x00: collisionOffset (u32) - offset to collision HitFileHeader
//   0x04: zoneOffset (u32) - offset to zone HitFileHeader
//
// HitFileHeader (0x18 bytes) at collisionOffset and zoneOffset:
//   0x00: numColliders (s16)
//   0x02: pad (2 bytes)
//   0x04: collidersOffset (s32) - offset to HitAssetCollider array
//   0x08: numVertices (s16)
//   0x0A: pad (2 bytes)
//   0x0C: verticesOffset (s32) - offset to Vec3s array
//   0x10: boundingBoxesDataSize (s16)
//   0x12: pad (2 bytes)
//   0x14: boundingBoxesOffset (s32) - offset to bounding box data
//
// HitAssetCollider (0x0C bytes) - array of numColliders entries:
//   0x00: boundingBoxOffset (s16)
//   0x02: nextSibling (s16)
//   0x04: firstChild (s16)
//   0x06: numTriangles (s16)
//   0x08: trianglesOffset (s32) - offset to packed triangle data
//
// Vertices: Vec3s array (6 bytes each) - numVertices entries
//   Each x, y, z is s16
//
// Triangles: s32 array - packed vertex indices + flags
//   bits 0-9: v1, 10-19: v2, 20-29: v3, 30: oneSided

static void ByteSwapHitFileHeader(uint8_t* data, uint32_t headerOffset, size_t totalSize) {
    if (headerOffset == 0 || headerOffset + 0x18 > totalSize) {
        return;
    }

    uint8_t* header = data + headerOffset;

    // numColliders at 0x00 (s16)
    uint16_t* numColliders = reinterpret_cast<uint16_t*>(header);
    *numColliders = BSWAP16(*numColliders);

    // collidersOffset at 0x04 (s32)
    uint32_t* collidersOffset = reinterpret_cast<uint32_t*>(header + 0x04);
    *collidersOffset = BSWAP32(*collidersOffset);

    // numVertices at 0x08 (s16)
    uint16_t* numVertices = reinterpret_cast<uint16_t*>(header + 0x08);
    *numVertices = BSWAP16(*numVertices);

    // verticesOffset at 0x0C (s32)
    uint32_t* verticesOffset = reinterpret_cast<uint32_t*>(header + 0x0C);
    *verticesOffset = BSWAP32(*verticesOffset);

    // boundingBoxesDataSize at 0x10 (s16)
    uint16_t* boundingBoxesDataSize = reinterpret_cast<uint16_t*>(header + 0x10);
    *boundingBoxesDataSize = BSWAP16(*boundingBoxesDataSize);

    // boundingBoxesOffset at 0x14 (s32)
    uint32_t* boundingBoxesOffset = reinterpret_cast<uint32_t*>(header + 0x14);
    *boundingBoxesOffset = BSWAP32(*boundingBoxesOffset);

    SPDLOG_DEBUG("HitFileHeader at 0x{:X}: numColliders={}, collidersOffset=0x{:X}, numVertices={}, "
                 "verticesOffset=0x{:X}, bbSize={}, bbOffset=0x{:X}",
                 headerOffset, *numColliders, *collidersOffset, *numVertices, *verticesOffset, *boundingBoxesDataSize,
                 *boundingBoxesOffset);

    // Byte-swap colliders array (HitAssetCollider, 0x0C bytes each)
    uint32_t collOffset = *collidersOffset;
    uint16_t numColl = *numColliders;
    if (collOffset > 0 && collOffset + numColl * 0x0C <= totalSize) {
        for (uint16_t i = 0; i < numColl; i++) {
            uint8_t* collider = data + collOffset + i * 0x0C;
            uint16_t* s16Fields = reinterpret_cast<uint16_t*>(collider);
            s16Fields[0] = BSWAP16(s16Fields[0]); // boundingBoxOffset
            s16Fields[1] = BSWAP16(s16Fields[1]); // nextSibling
            s16Fields[2] = BSWAP16(s16Fields[2]); // firstChild
            s16Fields[3] = BSWAP16(s16Fields[3]); // numTriangles
            uint32_t* trianglesOffset = reinterpret_cast<uint32_t*>(collider + 0x08);
            *trianglesOffset = BSWAP32(*trianglesOffset);

            // Byte-swap triangles array for this collider (s32 each)
            uint16_t numTris = s16Fields[3];
            uint32_t trisOffset = *trianglesOffset;
            if (numTris > 0 && trisOffset > 0 && trisOffset + numTris * 4 <= totalSize) {
                uint32_t* triangles = reinterpret_cast<uint32_t*>(data + trisOffset);
                for (uint16_t t = 0; t < numTris; t++) {
                    triangles[t] = BSWAP32(triangles[t]);
                }
            }
        }
    }

    // Byte-swap vertices array (Vec3s, 6 bytes each - 3 x s16)
    uint32_t vertOffset = *verticesOffset;
    uint16_t numVerts = *numVertices;
    if (vertOffset > 0 && vertOffset + numVerts * 6 <= totalSize) {
        uint16_t* vertices = reinterpret_cast<uint16_t*>(data + vertOffset);
        for (uint16_t i = 0; i < numVerts * 3; i++) {
            vertices[i] = BSWAP16(vertices[i]);
        }
    }

    // Byte-swap bounding boxes data (u32 array)
    uint32_t bbOffset = *boundingBoxesOffset;
    uint16_t bbSize = *boundingBoxesDataSize;
    if (bbOffset > 0 && bbSize > 0 && bbOffset + bbSize * 4 <= totalSize) {
        uint32_t* bboxes = reinterpret_cast<uint32_t*>(data + bbOffset);
        for (uint16_t i = 0; i < bbSize; i++) {
            bboxes[i] = BSWAP32(bboxes[i]);
        }
    }
}

static void ByteSwapCollisionData(uint8_t* data, size_t size) {
    if (size < 8) {
        SPDLOG_WARN("Collision data too small: {}", size);
        return;
    }

    // Byte-swap HitFile header
    uint32_t* header = reinterpret_cast<uint32_t*>(data);
    uint32_t collisionOffset = BSWAP32(header[0]);
    uint32_t zoneOffset = BSWAP32(header[1]);
    header[0] = collisionOffset;
    header[1] = zoneOffset;

    SPDLOG_DEBUG("HitFile: collisionOffset=0x{:X}, zoneOffset=0x{:X}", collisionOffset, zoneOffset);

    // Byte-swap collision HitFileHeader and its data
    ByteSwapHitFileHeader(data, collisionOffset, size);

    // Byte-swap zone HitFileHeader and its data
    ByteSwapHitFileHeader(data, zoneOffset, size);
}

std::optional<std::shared_ptr<IParsedData>> PM64CollisionFactory::parse(std::vector<uint8_t>& buffer,
                                                                        YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    // Check if compressed (YAY0)
    auto compressionType = Decompressor::GetCompressionType(buffer, offset);

    if (compressionType == CompressionType::YAY0) {
        auto decoded = Decompressor::Decode(buffer, offset, CompressionType::YAY0);
        if (!decoded || decoded->size == 0) {
            SPDLOG_ERROR("Failed to decompress YAY0 collision data at offset 0x{:X}", offset);
            return std::nullopt;
        }

        std::vector<uint8_t> colData(decoded->data, decoded->data + decoded->size);
        ByteSwapCollisionData(colData.data(), colData.size());

        return std::make_shared<RawBuffer>(colData);
    } else {
        // Uncompressed - read raw data with size from YAML
        auto size = GetSafeNode<size_t>(node, "size");
        auto [_, segment] = Decompressor::AutoDecode(node, buffer, size);

        std::vector<uint8_t> colData(segment.data, segment.data + segment.size);
        ByteSwapCollisionData(colData.data(), colData.size());

        return std::make_shared<RawBuffer>(colData);
    }
}

ExportResult PM64CollisionBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                 std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    // Write as Blob type - game loads as raw binary
    WriteHeader(writer, Torch::ResourceType::Blob, 0);
    writer.Write(static_cast<uint32_t>(data.size()));
    writer.Write(reinterpret_cast<char*>(data.data()), data.size());
    writer.Finish(write);

    return std::nullopt;
}

ExportResult PM64CollisionHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                 std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern u8 " << symbol << "[];\n";
    return std::nullopt;
}

#ifdef BUILD_UI
#include <cmath>
#include <cstring>
#include <map>
#include "imgui.h"
#include "ui/BaseBackend.h"
#include "ui/Widgets.h"

namespace {

uint32_t CRD32(const std::vector<uint8_t>& b, uint32_t off) {
    if (off + 4 > b.size()) {
        return 0;
    }
    uint32_t v;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}

int16_t CRD16(const std::vector<uint8_t>& b, uint32_t off) {
    if (off + 2 > b.size()) {
        return 0;
    }
    int16_t v;
    std::memcpy(&v, b.data() + off, 2);
    return v;
}

std::map<std::string, std::vector<UI::PreviewVertex>> sHitTris;
std::map<std::string, UI::OrbitView> sHitViews;

const std::vector<UI::PreviewVertex>& HitTris(const ParseResultData& item) {
    auto it = sHitTris.find(item.name);
    if (it != sHitTris.end()) {
        return it->second;
    }
    std::vector<UI::PreviewVertex> tris;
    const auto& d = std::static_pointer_cast<RawBuffer>(item.data.value())->mBuffer;
    const uint32_t collisionOff = CRD32(d, 0);
    if (collisionOff != 0 && collisionOff + 0x18 <= d.size()) {
        const int16_t numColliders = CRD16(d, collisionOff);
        const uint32_t collidersOff = CRD32(d, collisionOff + 0x04);
        const int16_t numVertices = CRD16(d, collisionOff + 0x08);
        const uint32_t verticesOff = CRD32(d, collisionOff + 0x0C);
        for (int16_t c = 0; c < numColliders; ++c) {
            const uint32_t col = collidersOff + (uint32_t)c * 0xC;
            const int16_t numTris = CRD16(d, col + 0x06);
            const uint32_t trisOff = CRD32(d, col + 0x08);
            // Per-collider hue so regions read apart.
            const float hue = (float)((c * 47) % 360) / 360.0f;
            const float rr = 0.5f + 0.5f * std::cos(6.2832f * hue);
            const float gg = 0.5f + 0.5f * std::cos(6.2832f * (hue + 0.33f));
            const float bb = 0.5f + 0.5f * std::cos(6.2832f * (hue + 0.67f));
            for (int16_t t = 0; t < numTris; ++t) {
                const uint32_t packed = CRD32(d, trisOff + (uint32_t)t * 4);
                const uint32_t idx[3] = { packed & 0x3FF, (packed >> 10) & 0x3FF, (packed >> 20) & 0x3FF };
                float pos[3][3];
                bool ok = true;
                for (int k = 0; k < 3 && ok; ++k) {
                    if ((int32_t)idx[k] >= numVertices) {
                        ok = false;
                        break;
                    }
                    const uint32_t v = verticesOff + idx[k] * 6;
                    pos[k][0] = (float)CRD16(d, v);
                    pos[k][1] = (float)CRD16(d, v + 2);
                    pos[k][2] = (float)CRD16(d, v + 4);
                }
                if (!ok) {
                    continue;
                }
                // Baked shading from the face normal.
                const float ux = pos[1][0] - pos[0][0], uy = pos[1][1] - pos[0][1], uz = pos[1][2] - pos[0][2];
                const float vx = pos[2][0] - pos[0][0], vy = pos[2][1] - pos[0][1], vz = pos[2][2] - pos[0][2];
                float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nl > 0.0001f) {
                    nx /= nl;
                    ny /= nl;
                    nz /= nl;
                }
                const float light = 0.55f + 0.45f * std::max(0.0f, nx * 0.3f + ny * 0.8f + nz * 0.52f);
                for (int k = 0; k < 3; ++k) {
                    UI::PreviewVertex pv{};
                    pv.position[0] = pos[k][0];
                    pv.position[1] = pos[k][1];
                    pv.position[2] = pos[k][2];
                    pv.color[0] = (unsigned char)(rr * light * 255.0f);
                    pv.color[1] = (unsigned char)(gg * light * 255.0f);
                    pv.color[2] = (unsigned char)(bb * light * 255.0f);
                    pv.color[3] = 255;
                    tris.push_back(pv);
                }
            }
        }
    }
    return sHitTris.emplace(item.name, std::move(tris)).first->second;
}

} // namespace

float PM64CollisionFactoryUI::GetItemHeight(const ParseResultData& item) {
    return 60.0f + UI::PreviewBlockHeight(item.name);
}

void PM64CollisionFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto& tris = HitTris(item);
    ImGui::TextDisabled("collision  \xe2\x80\x94  %zu tris", tris.size() / 3);
    if (tris.empty()) {
        ImGui::TextDisabled("nothing drawable");
        return;
    }
    UI::OrbitView& view = sHitViews[item.name];
    const UI::PreviewCanvas canvas = UI::BeginResizableCanvas("##hitview", item.name, view);
    if (canvas.visible) {
        UI::GetBackend()->DrawTriangles(item.name, tris, canvas.origin, canvas.size, view);
    }
}
#endif // BUILD_UI
