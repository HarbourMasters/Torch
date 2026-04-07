#ifdef OOT_SUPPORT

#include "OoTDListHelpers.h"
#include "Companion.h"
#include "spdlog/spdlog.h"
#include "factories/DisplayListOverrides.h"
#include "n64/gbi-otr.h"
#include "strhash64/StrHash64.h"

namespace OoT {
namespace DListHelpers {

uint32_t RemapSegmentedAddr(uint32_t addr, const std::string& expectedType) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::OoT) return addr;

    uint8_t seg = SEGMENT_NUMBER(addr);
    uint32_t offset = SEGMENT_OFFSET(addr);
    auto segBase = Companion::Instance->GetFileOffsetFromSegmentedAddr(seg);
    if (!segBase.has_value()) return addr;

    for (uint8_t otherSeg = 1; otherSeg < 0x20; otherSeg++) {
        if (otherSeg == seg) continue;
        auto otherBase = Companion::Instance->GetFileOffsetFromSegmentedAddr(otherSeg);
        if (otherBase.has_value() && otherBase.value() == segBase.value()) {
            uint32_t remapped = (otherSeg << 24) | offset;
            auto node = Companion::Instance->GetNodeByAddr(remapped);
            if (node.has_value()) {
                if (!expectedType.empty()) {
                    auto n = std::get<1>(node.value());
                    auto nType = GetSafeNode<std::string>(n, "type");
                    if (nType != expectedType) continue;
                }
                return remapped;
            }
        }
    }
    return addr;
}

bool HandleGSunDLVtx(uint32_t w0, uint32_t w1,
                     LUS::BinaryWriter& writer, std::string* replacement) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::OoT) return false;
    if (!replacement || replacement->find("gSunDL") == std::string::npos) return false;

    auto ptr = w1;
    std::optional<std::pair<std::string, uint32_t>> rangedMatch;

    for (const auto& type : std::vector<std::string>{"TEXTURE", "BLOB"}) {
        auto decs = Companion::Instance->GetNodesByType(type);
        if (!decs.has_value()) continue;
        for (auto& [name, dnode] : decs.value()) {
            auto doffset = GetSafeNode<uint32_t>(dnode, "offset");
            uint32_t dsize = 0;
            if (type == "TEXTURE") {
                auto fmt = GetSafeNode<std::string>(dnode, "format");
                auto w = GetSafeNode<uint32_t>(dnode, "width");
                auto h = GetSafeNode<uint32_t>(dnode, "height");
                uint32_t bpp = 16;
                if (fmt == "I4" || fmt == "IA4" || fmt == "CI4") bpp = 4;
                else if (fmt == "I8" || fmt == "IA8" || fmt == "CI8") bpp = 8;
                else if (fmt == "RGBA16" || fmt == "IA16") bpp = 16;
                else if (fmt == "RGBA32") bpp = 32;
                dsize = (w * h * bpp) / 8;
            } else if (type == "BLOB") {
                dsize = GetSafeNode<uint32_t>(dnode, "size");
            }
            if (ASSET_PTR(ptr) >= ASSET_PTR(doffset) && ASSET_PTR(ptr) < ASSET_PTR(doffset) + dsize) {
                auto path = Companion::Instance->GetSafeStringByAddr(doffset, type);
                uint32_t diff = ASSET_PTR(ptr) - ASSET_PTR(doffset);
                if (path.has_value() && (!rangedMatch.has_value() || diff < rangedMatch->second)) {
                    rangedMatch = std::make_pair(path.value(), diff);
                }
            }
        }
    }

    if (!rangedMatch.has_value()) return false;

    auto& [path, diff] = rangedMatch.value();
    uint64_t hash = CRC64(path.c_str());
    size_t nvtx = (w0 >> 12) & 0xFF;  // C0(12, 8) for f3dex2
    size_t didx = ((w0 >> 1) & 0x7F) - nvtx;  // C0(1, 7) - C0(12, 8)
    N64Gfx value = gsSPVertexOTR(diff, nvtx, didx);
    writer.Write(value.words.w0);
    writer.Write(value.words.w1);
    writer.Write(static_cast<uint32_t>(hash >> 32));
    writer.Write(static_cast<uint32_t>(hash & 0xFFFFFFFF));
    return true;
}

bool HandleExportVtx(uint32_t& w0, uint32_t& w1, uint32_t& ptr,
                     size_t nvtx, size_t didx,
                     LUS::BinaryWriter& writer, std::string* replacement) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::OoT) return false;

    ptr = Companion::Instance->PatchVirtualAddr(w1);

    // Check if VTX is on an alias segment
    bool isAliasSegment = false;
    if (IS_SEGMENTED(w1)) {
        auto thisSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(w1));
        if (thisSeg.has_value()) {
            for (uint8_t s = 0; s < SEGMENT_NUMBER(w1); s++) {
                auto otherSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(s);
                if (otherSeg.has_value() && otherSeg.value() == thisSeg.value()) {
                    isAliasSegment = true;
                    break;
                }
            }
        } else {
            isAliasSegment = true;
        }
    }

    if (isAliasSegment) {
        w1 = w1 + 1;
        SPDLOG_INFO("VTX export: alias segment for 0x{:X}", ptr);
        return true;
    }

    // Check overlap with cross-file handling
    if (auto overlap = GFXDOverride::GetVtxOverlap(ptr); overlap.has_value()) {
        auto ovnode = std::get<1>(overlap.value());
        auto path = Companion::Instance->RelativePath(std::get<0>(overlap.value()));

        auto currentDir = (*replacement).substr(0, (*replacement).rfind('/'));
        auto vtxDir = path.substr(0, path.rfind('/'));
        if (currentDir != vtxDir) {
            SPDLOG_WARN("Cross-file VTX overlap at 0x{:X} (from {}), writing null vtxDecl", ptr, path);
            w0 = G_VTX_OTR_HASH << 24;
            w1 = 0;
        } else {
            uint64_t hash = CRC64(path.c_str());
            if (hash == 0) {
                throw std::runtime_error("Vtx hash is 0 for " + std::get<0>(overlap.value()));
            }
            SPDLOG_INFO("Found vtx: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, path);
            auto offset = GetSafeNode<uint32_t>(ovnode, "offset");
            auto diff = ASSET_PTR(ptr) - ASSET_PTR(offset);
            N64Gfx value = gsSPVertexOTR(diff, nvtx, didx);
            w0 = value.words.w0;
            w1 = value.words.w1;
            writer.Write(w0);
            writer.Write(w1);
            w0 = hash >> 32;
            w1 = hash & 0xFFFFFFFF;
        }
        return true;
    }

    // Direct lookup with OOT:ARRAY support
    auto vtxNode = Companion::Instance->GetNodeByAddr(ptr);
    std::optional<std::string> dec = std::nullopt;
    if (vtxNode.has_value()) {
        auto [vpath, vn] = vtxNode.value();
        auto vtype = GetSafeNode<std::string>(vn, "type");
        if (vtype == "VTX" || vtype == "OOT:ARRAY") {
            dec = vpath;
        }
    }
    if (dec.has_value()) {
        auto currentDir = (*replacement).substr(0, (*replacement).rfind('/'));
        auto vtxDir = dec.value().substr(0, dec.value().rfind('/'));
        if (currentDir != vtxDir) {
            SPDLOG_WARN("Cross-file VTX at 0x{:X} (from {}), writing null vtxDecl", ptr, dec.value());
            w0 = G_VTX_OTR_HASH << 24;
            w1 = 0;
        } else {
            uint64_t hash = CRC64(dec.value().c_str());
            if (hash == 0) {
                throw std::runtime_error("Vtx hash is 0 for " + dec.value());
            }
            SPDLOG_INFO("Found vtx: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, dec.value());
            N64Gfx value = gsSPVertexOTR(0, nvtx, didx);
            w0 = value.words.w0;
            w1 = value.words.w1;
            writer.Write(w0);
            writer.Write(w1);
            w0 = hash >> 32;
            w1 = hash & 0xFFFFFFFF;
        }
        return true;
    }

    // Virtual segment handling
    if (IS_VIRTUAL_SEGMENT(w1)) {
        w0 = G_VTX_OTR_HASH << 24;
        w1 = 0;
        return true;
    }

    // Cross-segment fallback
    SPDLOG_WARN("VTX export: NOT FOUND vtx at 0x{:X} w1=0x{:X} replacement={}", ptr, w1, *replacement);
    w1 = (w1 & 0x0FFFFFFF) + 1;
    return true;
}

std::optional<std::tuple<std::string, YAML::Node>> SearchVtx(uint32_t ptr) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::OoT) return std::nullopt;

    std::vector<std::string> vtxTypes = {"VTX", "OOT:ARRAY"};

    uint32_t absPtr = ptr;
    if (IS_SEGMENTED(ptr)) {
        auto seg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(ptr));
        if (!seg.has_value()) {
            return std::nullopt;
        }
        absPtr = seg.value() + SEGMENT_OFFSET(ptr);
    }

    for (const auto& type : vtxTypes) {
        auto decs = Companion::Instance->GetNodesByType(type);
        if (!decs.has_value()) continue;

        for (auto& dec : decs.value()) {
            auto [name, node] = dec;

            if (type == "OOT:ARRAY") {
                auto arrayType = GetSafeNode<std::string>(node, "array_type", "");
                if (arrayType != "VTX") continue;
            }

            auto offset = GetSafeNode<uint32_t>(node, "offset");
            auto count = GetSafeNode<uint32_t>(node, "count");
            auto end = ((count * 16 + 0xF) & ~0xF); // ALIGN16(count * sizeof(N64Vtx_t))

            uint32_t absOffset = offset;
            if (IS_SEGMENTED(offset)) {
                auto seg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(offset));
                if (!seg.has_value()) continue;
                absOffset = seg.value() + SEGMENT_OFFSET(offset);
            }

            if (absPtr > absOffset && absPtr < absOffset + end) {
                return std::make_tuple(GetSafeNode<std::string>(node, "symbol", name), node);
            }
        }
    }

    return std::nullopt;
}

} // namespace DListHelpers
} // namespace OoT

#endif
