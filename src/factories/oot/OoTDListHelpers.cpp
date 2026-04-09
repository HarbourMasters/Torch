#ifdef OOT_SUPPORT

#include "OoTDListHelpers.h"
#include "Companion.h"
#include "spdlog/spdlog.h"
#include "factories/DisplayListFactory.h"
#include "factories/DisplayListOverrides.h"
#include "utils/Decompressor.h"
#include "DeferredVtx.h"
#include <iomanip>
#include <sstream>
#include "n64/gbi-otr.h"
#include "strhash64/StrHash64.h"

#define C0(pos, width) ((w0 >> (pos)) & ((1U << width) - 1))
#define ALIGN16(val) (((val) + 0xF) & ~0xF)

// F3DEX2 opcodes (OoT is always f3dex2)
static constexpr uint8_t F3DEX2_VTX = 0x01;
static constexpr uint8_t F3DEX2_DL = 0xDE;
static constexpr uint8_t F3DEX2_MTX = 0xDA;
static constexpr uint8_t F3DEX2_ENDDL = 0xDF;
static constexpr uint8_t F3DEX2_SETTIMG = 0xFD;
static constexpr uint8_t F3DEX2_MOVEMEM = 0xDC;
static constexpr uint8_t F3DEX2_RDPHALF_1 = 0xE1;
static constexpr uint8_t F3DEX2_BRANCH_Z = 0x04;
static constexpr uint8_t F3DEX2_SETOTHERMODE_H = 0xE3;
static constexpr uint8_t F3DEX2_NOOP = 0x00;
static constexpr uint8_t F3DEX2_SETTILE = 0xF5;
static constexpr uint8_t F3DEX2_LOADBLOCK = 0xF3;
static constexpr uint8_t F3DEX2_MV_LIGHT = 0x0A;

namespace OoT {
namespace DListHelpers {

// ── Internal helpers (not exposed in header) ──────────────────────────

static uint32_t RemapSegmentedAddr(uint32_t addr, const std::string& expectedType = "") {
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

static bool IsAliasSegment(uint32_t addr) {
    if (!IS_SEGMENTED(addr)) return false;
    auto thisSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(addr));
    if (!thisSeg.has_value()) return true;
    for (uint8_t s = 0; s < SEGMENT_NUMBER(addr); s++) {
        auto otherSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(s);
        if (otherSeg.has_value() && otherSeg.value() == thisSeg.value()) {
            return true;
        }
    }
    return false;
}

// ── Export helpers ─────────────────────────────────────────────────────
// All Export helpers follow main's pattern: they modify w0/w1 in place and may write
// intermediate words. The loop's final writer.Write(w0); writer.Write(w1) handles the last pair.
// Helpers that need to skip the rest of the iteration (gSunDLVtx, BranchZ, RDPHALF_1)
// write all their words and return true for `continue`.

static bool ExportGSunDLVtx(uint32_t w0, uint32_t w1,
                            LUS::BinaryWriter& writer, std::string* replacement) {
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
    size_t nvtx = (w0 >> 12) & 0xFF;
    size_t didx = ((w0 >> 1) & 0x7F) - nvtx;
    N64Gfx value = gsSPVertexOTR(diff, nvtx, didx);
    writer.Write(value.words.w0);
    writer.Write(value.words.w1);
    writer.Write(static_cast<uint32_t>(hash >> 32));
    writer.Write(static_cast<uint32_t>(hash & 0xFFFFFFFF));
    return true; // All words written, caller should continue
}

// Modifies w0/w1 for the final write. Writes intermediate words.
static void ExportVtx(uint32_t& w0, uint32_t& w1,
                      LUS::BinaryWriter& writer, std::string* replacement) {
    size_t nvtx = (w0 >> 12) & 0xFF; // C0(12, 8)
    size_t didx = ((w0 >> 1) & 0x7F) - nvtx; // C0(1, 7) - C0(12, 8)
    auto ptr = Companion::Instance->PatchVirtualAddr(w1);

    if (IsAliasSegment(w1)) {
        w1 = w1 + 1;
        SPDLOG_INFO("VTX export: alias segment for 0x{:X}", ptr);
        return; // w0/w1 set, caller writes final pair
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
            writer.Write(value.words.w0);
            writer.Write(value.words.w1);
            w0 = hash >> 32;
            w1 = hash & 0xFFFFFFFF;
        }
        return;
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
            writer.Write(value.words.w0);
            writer.Write(value.words.w1);
            w0 = hash >> 32;
            w1 = hash & 0xFFFFFFFF;
        }
        return;
    }

    // Virtual segment handling
    if (IS_VIRTUAL_SEGMENT(w1)) {
        w0 = G_VTX_OTR_HASH << 24;
        w1 = 0;
        return;
    }

    // Cross-segment fallback
    SPDLOG_WARN("VTX export: NOT FOUND vtx at 0x{:X} w1=0x{:X} replacement={}", ptr, w1, *replacement);
    w1 = (w1 & 0x0FFFFFFF) + 1;
}

static void ExportDL(uint32_t& w0, uint32_t& w1, LUS::BinaryWriter& writer) {
    auto ptr = w1;
    uint8_t dlSeg = SEGMENT_NUMBER(ptr);

    // Segments 8-13 are runtime-swapped and must stay unresolved
    std::optional<std::string> dec = std::nullopt;
    if (dlSeg < 8 || dlSeg > 13) {
        dec = Companion::Instance->GetSafeStringByAddr(ptr, "GFX");
        if (!dec.has_value()) {
            auto remapped = RemapSegmentedAddr(ptr, "GFX");
            if (remapped != ptr) {
                dec = Companion::Instance->GetSafeStringByAddr(remapped, "GFX");
                if (dec.has_value()) ptr = remapped;
            }
        }
    }
    auto branch = (w0 >> 16) & G_DL_NO_PUSH;

    if (dec.has_value()) {
        uint64_t hash = CRC64(dec.value().c_str());
        SPDLOG_INFO("Found display list: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, dec.value());

        N64Gfx value = gsSPDisplayListOTRHash(ptr);
        w0 = value.words.w0;
        w1 = 0;

        writer.Write(w0);
        writer.Write(w1);

        w0 = hash >> 32;
        w1 = hash & 0xFFFFFFFF;

        if (branch) {
            writer.Write(w0);
            writer.Write(w1);

            N64Gfx endValue = gsSPRawOpcode(F3DEX2_ENDDL);
            w0 = endValue.words.w0;
            w1 = endValue.words.w1;
        }
    } else {
        SPDLOG_WARN("Could not find display list at 0x{:X}", ptr);
        w1 = (w1 & 0x0FFFFFFF) + 1;
    }
}

static void ExportMoveMem(uint32_t& w0, uint32_t& w1, LUS::BinaryWriter& writer) {
    auto ptr = w1;
    uint8_t index = (w0) & 0xFF; // C0(0, 8)
    uint8_t offset = ((w0 >> 8) & 0xFF) * 8; // C0(8, 8) * 8
    bool hasOffset = false;

    auto res = Companion::Instance->GetStringByAddr(ptr);

    if (!res.has_value()) {
        res = Companion::Instance->GetStringByAddr(ptr - 0x8);
        hasOffset = res.has_value();
    }

    if (res.has_value()) {
        uint64_t hash = CRC64(res.value().c_str());
        SPDLOG_INFO("Found movemem: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, res.value());

        w0 &= 0x00FFFFFF;
        w0 += G_MOVEMEM_OTR_HASH << 24;
        w1 = _SHIFTL(index, 24, 8) | _SHIFTL(offset, 16, 8) | _SHIFTL((uint8_t)(hasOffset ? 1 : 0), 8, 8);

        writer.Write(w0);
        writer.Write(w1);

        w0 = hash >> 32;
        w1 = hash & 0xFFFFFFFF;
    } else {
        SPDLOG_WARN("Could not find light at 0x{:X}", ptr);
    }
    // Final w0/w1 written by caller
}

static void ExportSetTImg(uint32_t& w0, uint32_t& w1,
                          LUS::BinaryWriter& writer, std::string* replacement) {
    auto ptr = w1;
    auto dec = Companion::Instance->GetSafeStringByAddr(ptr, "TEXTURE");

    if (dec.has_value()) {
        uint64_t hash = CRC64(dec.value().c_str());
        if (hash == 0) {
            throw std::runtime_error("Texture hash is 0 for " + dec.value());
        }
        SPDLOG_INFO("Found texture: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, dec.value());

        uint32_t newW0 = (G_SETTIMG_OTR_HASH << 24) | (w0 & 0x00FFFFFF);
        writer.Write(newW0);
        writer.Write(static_cast<uint32_t>(0));

        w0 = hash >> 32;
        w1 = hash & 0xFFFFFFFF;
    } else {
        SPDLOG_WARN("Could not find texture at 0x{:X}", ptr);
        if (replacement && replacement->find("sShadowMaterialDL") != std::string::npos) {
            w1 = 0x0C000001;
        } else {
            auto patchedPtr = Companion::Instance->PatchVirtualAddr(ptr);
            w1 = (patchedPtr & 0x0FFFFFFF) + 1;
        }
        writer.Write(w0);
        writer.Write(w1);
    }
    // Final w0/w1 written by caller
}

static void ExportGSunDLTextureFixup(uint8_t opcode, uint32_t& w0, uint32_t& w1,
                                     std::string* replacement) {
    if (!replacement || replacement->find("gSunDL") == std::string::npos) return;

    constexpr uint8_t G_TX_LOADTILE = 7;
    constexpr uint8_t G_IM_SIZ_4b = 0;

    if (opcode == F3DEX2_SETTILE) {
        uint8_t tile = (w1 >> 24) & 0x07;
        if (tile != G_TX_LOADTILE) {
            w0 = (w0 & ~(0x3 << 19)) | (G_IM_SIZ_4b << 19);
        }
    }

    if (opcode == F3DEX2_LOADBLOCK) {
        uint32_t ult = w0 & 0xFFF;
        uint32_t texels = (w1 >> 12) & 0xFFF;
        if (ult != G_TX_LOADTILE) {
            texels = (texels + 1) / 2 - 1;
            w1 = (w1 & ~(0xFFF << 12)) | ((texels & 0xFFF) << 12);
        }
    }
}

static void ExportMtx(uint32_t& w0, uint32_t& w1, LUS::BinaryWriter& writer) {
    auto ptr = w1;
    auto dec = Companion::Instance->GetSafeStringByAddr(ptr, "OOT:MTX");
    if (!dec.has_value()) {
        dec = Companion::Instance->GetSafeStringByAddr(ptr, "MTX");
    }
    if (!dec.has_value()) {
        auto remapped = RemapSegmentedAddr(ptr, "OOT:MTX");
        if (remapped == ptr) remapped = RemapSegmentedAddr(ptr, "MTX");
        if (remapped != ptr) {
            dec = Companion::Instance->GetSafeStringByAddr(remapped, "OOT:MTX");
            if (!dec.has_value()) dec = Companion::Instance->GetSafeStringByAddr(remapped, "MTX");
            if (dec.has_value()) ptr = remapped;
        }
    }

    if (dec.has_value()) {
        uint64_t hash = CRC64(dec.value().c_str());
        if (hash == 0) {
            throw std::runtime_error("Matrix hash is 0 for " + dec.value());
        }
        SPDLOG_INFO("Found matrix: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, dec.value());

        w0 &= 0x00FFFFFF;
        w0 += G_MTX_OTR << 24;

        writer.Write(w0);
        writer.Write(w1);

        w0 = hash >> 32;
        w1 = hash & 0xFFFFFFFF;
    } else {
        SPDLOG_WARN("Could not find matrix at 0x{:X}", ptr);
        w1 = (ptr & 0x0FFFFFFF) + 1;
    }
    // Final w0/w1 written by caller
}

// Writes all words and returns true (caller should continue).
static bool ExportBranchZ(uint32_t w0, uint32_t w1,
                          size_t cmdIndex, const std::vector<uint32_t>& cmds,
                          LUS::BinaryWriter& writer) {
    uint32_t dlAddr = (cmdIndex >= 2) ? cmds[cmdIndex - 1] : 0;
    auto dec = Companion::Instance->GetSafeStringByAddr(dlAddr, "GFX");
    if (!dec.has_value()) {
        auto remapped = RemapSegmentedAddr(dlAddr, "GFX");
        if (remapped != dlAddr) {
            dec = Companion::Instance->GetSafeStringByAddr(remapped, "GFX");
        }
    }

    if (dec.has_value()) {
        uint64_t hash = CRC64(dec.value().c_str());
        uint32_t a = (w0 >> 12) & 0xFFF;
        uint32_t b = w0 & 0xFFF;
        uint32_t branchW0 = (G_BRANCH_Z_OTR << 24) | _SHIFTL(a, 12, 12) | _SHIFTL(b, 0, 12);
        writer.Write(branchW0);
        writer.Write(w1);
        writer.Write(static_cast<uint32_t>(hash >> 32));
        writer.Write(static_cast<uint32_t>(hash & 0xFFFFFFFF));
    } else {
        SPDLOG_WARN("Could not find display list for G_BRANCH_Z at 0x{:X}", dlAddr);
        writer.Write(w0);
        writer.Write(w1);
    }
    return true; // All words written, caller should continue
}

static void ExportOpcodeFixups(uint8_t opcode, uint32_t& w0, uint32_t& w1) {
    // G_SETOTHERMODE_H texture LUT re-encoding
    if (opcode == F3DEX2_SETOTHERMODE_H) {
        uint8_t ss = (w0 >> 8) & 0xFF;
        uint8_t nn = w0 & 0xFF;
        int32_t sft = 32 - (nn + 1) - ss;
        if (sft == 14) {
            w1 = w1 >> 14;
        }
    }

    // G_NOOP zeroing
    if (opcode == F3DEX2_NOOP) {
        w0 = 0;
        w1 = 0;
    }

    // Unhandled opcode zeroing
    static const std::unordered_set<uint8_t> otrHandledOpcodes = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDE, 0xDF,
        0xE1, 0xE2, 0xE3, 0xE4,
        0xE6, 0xE7, 0xE8, 0xE9, 0xEF,
        0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5,
        0xFA, 0xFB, 0xFC, 0xFD,
    };
    if (otrHandledOpcodes.find(opcode) == otrHandledOpcodes.end()) {
        w0 = (uint32_t)opcode << 24;
        w1 = 0;
    }
}

// ── Parse helpers ─────────────────────────────────────────────────────

static void ParseDL(uint32_t w0, uint32_t w1, YAML::Node& node) {
    if (C0(16, 1) == G_DL_NO_PUSH) {
        // Branch — handled by caller (sets processing = false)
    }

    if (SEGMENT_NUMBER(node["offset"].as<uint32_t>()) == SEGMENT_NUMBER(w1)) {
        // OoT pre-declares child DLists in YAML — skip AddAsset
        auto parentSymbol = GetSafeNode<std::string>(node, "symbol", "");
        auto dlPos = parentSymbol.rfind("DL_");
        if (dlPos != std::string::npos) {
            auto base = parentSymbol.substr(0, dlPos + 3);
            uint32_t childOffset = SEGMENT_OFFSET(w1);
            std::ostringstream ss;
            ss << base << std::uppercase << std::hex
               << std::setfill('0') << std::setw(6) << childOffset;
            // Symbol derived but not added (OoT pre-declares in YAML)
        }
    }
}

static void ParseMoveMem(uint32_t w0, uint32_t w1) {
    uint8_t subcommand = (w0 >> 16) & 0xFF;
    uint8_t index = C0(0, 8);
    uint8_t offset = C0(8, 8) * 8;
    bool light = false;

    // OoT is f3dex2 only — check G_MV_LIGHT
    if (index == F3DEX2_MV_LIGHT && offset == (2 * 24 + 24)) {
        light = true;
    }

    if (light) {
        // OoT pre-declares lights in YAML — skip AddAsset
    }
}

static void ParseRdpHalf1(uint32_t w0, uint32_t w1,
                          YAML::Node& node, std::vector<uint8_t>& buffer) {
    if (!IS_SEGMENTED(w1) || SEGMENT_NUMBER(w1) != SEGMENT_NUMBER(node["offset"].as<uint32_t>())) {
        return;
    }

    const auto decl = Companion::Instance->GetNodeByAddr(w1);
    if (!decl.has_value()) {
        // DList pre-declared in YAML — skip AddAsset
    }

    if (DeferredVtx::IsDeferred()) {
        auto branchData = Decompressor::AutoDecode(w1, std::nullopt, buffer);
        LUS::BinaryReader branchReader(branchData.segment.data, branchData.segment.size);
        branchReader.SetEndianness(Torch::Endianness::Big);

        while (branchReader.GetBaseAddress() + 8 <= branchData.segment.size) {
            auto bw0 = branchReader.ReadUInt32();
            auto bw1 = branchReader.ReadUInt32();
            uint8_t bOpcode = bw0 >> 24;

            if (bOpcode == F3DEX2_ENDDL) break;

            if (bOpcode == F3DEX2_VTX && IS_SEGMENTED(bw1)) {
                uint32_t bNvtx = (bw0 >> 12) & 0xFF;
                DeferredVtx::AddPending(bw1, bNvtx);
            }
        }
    }
}

static void ParseMtx(uint32_t w1, YAML::Node& node) {
    if (IS_SEGMENTED(w1) && SEGMENT_NUMBER(w1) == SEGMENT_NUMBER(node["offset"].as<uint32_t>())) {
        const auto decl = Companion::Instance->GetNodeByAddr(w1);
        if (!decl.has_value()) {
            SPDLOG_WARN("Undeclared MTX at 0x{:08X} — YAML enrichment incomplete", w1);
        }
    }
}

static void ParseVtx(uint32_t w0, uint32_t w1, uint32_t nvtx,
                     YAML::Node& node, std::vector<uint8_t>& buffer) {
    const auto decl = Companion::Instance->GetNodeByAddr(w1);
    if (decl.has_value()) {
        SPDLOG_WARN("Found vtx at 0x{:X}", w1);
        return;
    }

    auto adjPtr = Companion::Instance->PatchVirtualAddr(w1);
    auto search = SearchVtx(adjPtr);

    if (search.has_value()) {
        auto [path, vtx] = search.value();
        SPDLOG_INFO("Path: {}", path);

        auto lOffset = GetSafeNode<uint32_t>(vtx, "offset");
        auto lCount = GetSafeNode<uint32_t>(vtx, "count");
        auto lSize = ALIGN16(lCount * 16); // sizeof(N64Vtx_t)

        // Compare in absolute ROM address space
        uint32_t absPtr = adjPtr;
        uint32_t absOffset = lOffset;
        if (IS_SEGMENTED(adjPtr)) {
            auto seg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(adjPtr));
            if (seg.has_value()) absPtr = seg.value() + SEGMENT_OFFSET(adjPtr);
        }
        if (IS_SEGMENTED(lOffset)) {
            auto seg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(lOffset));
            if (seg.has_value()) absOffset = seg.value() + SEGMENT_OFFSET(lOffset);
        }

        if (absPtr > absOffset && absPtr <= absOffset + lSize) {
            SPDLOG_INFO("Found vtx at 0x{:X} matching last vtx at 0x{:X}", adjPtr, lOffset);
            GFXDOverride::RegisterVTXOverlap(adjPtr, search.value());
        }
        return;
    }

    // Skip VTX auto-creation for alias segments, virtual addresses, etc.
    bool skipVtx = !IS_SEGMENTED(adjPtr) || IsAliasSegment(adjPtr);

    if (!skipVtx) {
        if (DeferredVtx::IsDeferred()) {
            DeferredVtx::AddPending(adjPtr, nvtx);
        } else {
            SPDLOG_WARN("Undeclared VTX at 0x{:08X} — YAML enrichment incomplete", adjPtr);
        }
    }
}

static void FlushVtx(YAML::Node& node) {
    if (!DeferredVtx::IsDeferred()) return;

    auto symbol = GetSafeNode<std::string>(node, "symbol", "");
    auto dlPos = symbol.rfind("DL_");
    std::string baseName = (dlPos != std::string::npos) ? symbol.substr(0, dlPos) : symbol;
    DeferredVtx::FlushDeferred(baseName);
}

// ── Public API ────────────────────────────────────────────────────────

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
            auto end = ALIGN16(count * 16); // sizeof(N64Vtx_t)

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

std::optional<ExportResult> Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                  std::string& entryName, YAML::Node& node, std::string* replacement) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::OoT) return std::nullopt;

    auto cmds = std::static_pointer_cast<DListData>(raw)->mGfxs;
    auto writer = LUS::BinaryWriter();

    BaseExporter::WriteHeader(writer, Torch::ResourceType::DisplayList, 0);

    writer.Write((int8_t)GBIVersion::f3dex2);

    while (writer.GetBaseAddress() % 8 != 0)
        writer.Write(static_cast<int8_t>(0xFF));

    auto bhash = CRC64((*replacement).c_str());
    writer.Write(static_cast<uint32_t>((G_MARKER << 24)));
    writer.Write(0xBEEFBEEF);
    writer.Write(static_cast<uint32_t>(bhash >> 32));
    writer.Write(static_cast<uint32_t>(bhash & 0xFFFFFFFF));

    for (size_t i = 0; i < cmds.size(); i += 2) {
        auto w0 = cmds[i];
        auto w1 = cmds[i + 1];
        uint8_t opcode = w0 >> 24;

        // gSunDL VTX override — writes all words, skip rest of iteration
        if (opcode == F3DEX2_VTX && ExportGSunDLVtx(w0, w1, writer, replacement)) {
            continue;
        }

        if (opcode == F3DEX2_VTX) {
            ExportVtx(w0, w1, writer, replacement);
        }

        if (opcode == F3DEX2_DL) {
            ExportDL(w0, w1, writer);
        }

        if (opcode == F3DEX2_MOVEMEM) {
            ExportMoveMem(w0, w1, writer);
        }

        if (opcode == F3DEX2_SETTIMG) {
            ExportSetTImg(w0, w1, writer, replacement);
        }

        if (opcode == F3DEX2_MTX) {
            ExportMtx(w0, w1, writer);
        }

        // RDPHALF_1 before BRANCH_Z — zero it out, BRANCH_Z writes both
        if (opcode == F3DEX2_RDPHALF_1 && i + 2 < cmds.size()) {
            uint8_t nextOpcode = cmds[i + 2] >> 24;
            if (nextOpcode == F3DEX2_BRANCH_Z) {
                writer.Write(static_cast<uint32_t>(0));
                writer.Write(static_cast<uint32_t>(0));
                continue;
            }
        }

        // BRANCH_Z — writes all words, skip rest of iteration
        if (opcode == F3DEX2_BRANCH_Z && ExportBranchZ(w0, w1, i, cmds, writer)) {
            continue;
        }

        // gSunDL texture format fixups (modifies w0/w1 in place)
        ExportGSunDLTextureFixup(opcode, w0, w1, replacement);

        // General opcode fixups (modifies w0/w1 in place)
        ExportOpcodeFixups(opcode, w0, w1);

        writer.Write(w0);
        writer.Write(w1);
    }

    writer.Finish(write);
    return ExportResult(std::nullopt);
}

std::optional<std::optional<std::shared_ptr<IParsedData>>> Parse(
        std::vector<uint8_t>& raw_buffer, YAML::Node& node) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::OoT) return std::nullopt;

    auto count = GetSafeNode<int32_t>(node, "count", -1);
    auto [_, segment] = Decompressor::AutoDecode(node, raw_buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<uint32_t> gfxs;
    auto processing = true;
    size_t length = 0;

    while (processing) {
        auto w0 = reader.ReadUInt32();
        auto w1 = reader.ReadUInt32();

        uint8_t opcode = w0 >> 24;

        if (opcode == F3DEX2_ENDDL) {
            processing = false;
        }

        if (opcode == F3DEX2_DL) {
            if (C0(16, 1) == G_DL_NO_PUSH) {
                SPDLOG_INFO("Branch List Command Found");
                processing = false;
            }
            ParseDL(w0, w1, node);
        }

        if (opcode == F3DEX2_MOVEMEM) {
            ParseMoveMem(w0, w1);
        }

        if (opcode == F3DEX2_RDPHALF_1) {
            ParseRdpHalf1(w0, w1, node, raw_buffer);
        }

        if (opcode == F3DEX2_MTX) {
            ParseMtx(w1, node);
        }

        if (opcode == F3DEX2_VTX) {
            uint32_t nvtx = C0(12, 8);
            ParseVtx(w0, w1, nvtx, node, raw_buffer);
        }

        if (count != -1 && length++ >= count) {
            break;
        }

        gfxs.push_back(w0);
        gfxs.push_back(w1);
    }

    FlushVtx(node);

    return std::make_optional(std::make_optional(
        std::static_pointer_cast<IParsedData>(std::make_shared<DListData>(gfxs))));
}

} // namespace DListHelpers
} // namespace OoT

#endif
