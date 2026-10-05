#include "MMDListHelpers.h"
#include "Companion.h"
#include "spdlog/spdlog.h"
#include "factories/DisplayListFactory.h"
#include "factories/DisplayListOverrides.h"
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

namespace MM {
namespace DListHelpers {


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

static bool ExportMMVtx(uint32_t& w0, uint32_t& w1, LUS::BinaryWriter& writer, std::string* replacement) {
    const auto ptr = Companion::Instance->PatchVirtualAddr(w1);
    const auto vtxNode = Companion::Instance->GetNodeByAddr(ptr);
    if (!vtxNode.has_value()) {
        return false;
    }

    auto [vpath, node] = vtxNode.value();
    if (GetSafeNode<std::string>(node, "type") != "MM:ARRAY" ||
        GetSafeNode<std::string>(node, "array_type", "") != "VTX") {
        return false;
    }

    const auto currentDir = replacement->substr(0, replacement->rfind('/'));
    const auto vtxDir = vpath.substr(0, vpath.rfind('/'));
    const bool nullCrossFile = node["null_cross_file"] && node["null_cross_file"].as<bool>();
    if (currentDir != vtxDir && nullCrossFile) {
        w0 = G_VTX_OTR_HASH << 24;
        w1 = 0;
        return true;
    }

    const uint64_t hash = CRC64(vpath.c_str());
    if (hash == 0) {
        throw std::runtime_error("Vtx hash is 0 for " + vpath);
    }

    const size_t count = (w0 >> 12) & 0xFF;
    const size_t index = ((w0 >> 1) & 0x7F) - count;
    const N64Gfx value = gsSPVertexOTR(0, count, index);
    writer.Write(value.words.w0);
    writer.Write(value.words.w1);
    w0 = hash >> 32;
    w1 = hash & 0xFFFFFFFF;
    return true;
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
        bool nullCrossFile = ovnode["null_cross_file"] && ovnode["null_cross_file"].as<bool>();
        if (currentDir != vtxDir && nullCrossFile) {
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
    bool nullCrossFile = false;
    if (vtxNode.has_value()) {
        auto [vpath, vn] = vtxNode.value();
        auto vtype = GetSafeNode<std::string>(vn, "type");
        if (vtype == "VTX" || vtype == "OOT:ARRAY") {
            dec = vpath;
            nullCrossFile = vn["null_cross_file"] && vn["null_cross_file"].as<bool>();
        }
    }
    if (dec.has_value()) {
        auto currentDir = (*replacement).substr(0, (*replacement).rfind('/'));
        auto vtxDir = dec.value().substr(0, dec.value().rfind('/'));
        if (currentDir != vtxDir && nullCrossFile) {
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

static void ExportMoveMem(uint32_t& w0, uint32_t& w1) {
    // OTRExporter has no G_MOVEMEM handler: its DisplayListExporter switch falls
    // through to the default (undefined-opcode) case, which emits just the opcode
    // byte with all operands zeroed (w0 = G_MOVEMEM << 24, w1 = 0). Match that
    // for byte-parity — a single 8-byte command written by the caller. (The
    // generic DisplayListFactory path keeps the G_MOVEMEM_OTR_HASH form for
    // other games; OoT follows OTRExporter here.)
    w0 = F3DEX2_MOVEMEM << 24;
    w1 = 0;
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

static std::optional<ExportResult> ExportBase(std::ostream& write, std::shared_ptr<IParsedData> raw,
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

        if (opcode == F3DEX2_VTX && !ExportMMVtx(w0, w1, writer, replacement)) {
            ExportVtx(w0, w1, writer, replacement);
        }

        if (opcode == F3DEX2_DL) {
            ExportDL(w0, w1, writer);
        }

        if (opcode == F3DEX2_MOVEMEM) {
            ExportMoveMem(w0, w1);
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

namespace {

uint32_t ToAbsoluteAddress(uint32_t address) {
    if (!IS_SEGMENTED(address)) {
        return address;
    }

    const auto segmentBase = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(address));
    if (!segmentBase.has_value()) {
        return address;
    }

    return segmentBase.value() + SEGMENT_OFFSET(address);
}

void RegisterMMVtxOverlaps(const std::shared_ptr<IParsedData>& raw) {
    const auto commands = std::static_pointer_cast<DListData>(raw)->mGfxs;
    auto arrays = Companion::Instance->GetNodesByType("MM:ARRAY");
    if (!arrays.has_value()) {
        return;
    }

    for (size_t i = 0; i + 1 < commands.size(); i += 2) {
        if ((commands[i] >> 24) != F3DEX2_VTX) {
            continue;
        }

        const uint32_t rawAddress = commands[i + 1];
        const uint32_t resolvedAddress = Companion::Instance->PatchVirtualAddr(rawAddress);

        const uint32_t absoluteAddress = ToAbsoluteAddress(resolvedAddress);

        for (auto& [_, array] : arrays.value()) {
            if (GetSafeNode<std::string>(array, "array_type", "") != "VTX") {
                continue;
            }

            const uint32_t offset = GetSafeNode<uint32_t>(array, "offset");
            const uint32_t count = GetSafeNode<uint32_t>(array, "count");
            const uint32_t absoluteOffset = ToAbsoluteAddress(offset);
            const uint32_t size = (count * sizeof(N64Vtx_t) + 0xF) & ~0xFU;

            if (absoluteAddress < absoluteOffset || absoluteAddress >= absoluteOffset + size) {
                continue;
            }

            std::tuple<std::string, YAML::Node> overlap = {
                GetSafeNode<std::string>(array, "symbol"), array
            };
            GFXDOverride::RegisterVTXOverlap(resolvedAddress, overlap);
            break;
        }
    }
}

std::string SerializeWords(uint32_t w0, uint32_t w1) {
    LUS::BinaryWriter writer;
    writer.Write(w0);
    writer.Write(w1);
    const auto bytes = writer.ToVector();
    return std::string(bytes.begin(), bytes.end());
}

void RewriteMMUnresolvedDLists(std::string& output, const std::shared_ptr<IParsedData>& raw) {
    const auto commands = std::static_pointer_cast<DListData>(raw)->mGfxs;

    for (size_t i = 0; i + 1 < commands.size(); i += 2) {
        const uint32_t w0 = commands[i];
        const uint32_t w1 = commands[i + 1];
        if ((w0 >> 24) != F3DEX2_DL || SEGMENT_OFFSET(w1) == 0) {
            continue;
        }

        const auto fallback = SerializeWords(w0, (w1 & 0x0FFFFFFF) + 1);
        N64Gfx index = gsSPDisplayListOTRIndex(w1);
        if (((w0 >> 16) & G_DL_NO_PUSH) != 0) {
            index.words.w0 |= 0x00010000;
        }
        const auto replacement = SerializeWords(index.words.w0, index.words.w1);

        size_t position = 0;
        while ((position = output.find(fallback, position)) != std::string::npos) {
            output.replace(position, fallback.size(), replacement);
            position += replacement.size();
        }
    }
}

void RewriteMMDuplicateMarkerHash(std::string& output, YAML::Node& node) {
    constexpr size_t kMarkerHashOffset = 0x50;
    constexpr size_t kMarkerHashSize = 8;
    if (!node["duplicate_of"] || output.size() < kMarkerHashOffset + kMarkerHashSize) {
        return;
    }

    const auto duplicateTarget = node["duplicate_of"].as<std::string>();
    const uint64_t hash = CRC64(duplicateTarget.c_str());
    LUS::BinaryWriter writer;
    writer.Write(static_cast<uint32_t>(hash >> 32));
    writer.Write(static_cast<uint32_t>(hash));
    const auto bytes = writer.ToVector();
    output.replace(kMarkerHashOffset, kMarkerHashSize, std::string(bytes.begin(), bytes.end()));
}



} // namespace

std::optional<ExportResult> Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                   std::string& entryName, YAML::Node& node, std::string* replacement) {
    RegisterMMVtxOverlaps(raw);
    std::ostringstream intermediate(std::ios::out | std::ios::binary);
    auto result = ExportBase(intermediate, raw, entryName, node, replacement);
    if (!result.has_value()) {
        return result;
    }

    auto output = intermediate.str();
    RewriteMMUnresolvedDLists(output, raw);
    RewriteMMDuplicateMarkerHash(output, node);
    write.write(output.data(), static_cast<std::streamsize>(output.size()));
    return result;
}
} // namespace DListHelpers
} // namespace MM
