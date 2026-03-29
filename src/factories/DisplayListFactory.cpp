#include "DisplayListFactory.h"
#include "DisplayListOverrides.h"
#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <fstream>
#include <iomanip>
#include "n64/gbi-otr.h"

#ifdef STANDALONE
#include <gfxd.h>
#endif

#define C0(pos, width) ((w0 >> (pos)) & ((1U << width) - 1))
#define ALIGN16(val) (((val) + 0xF) & ~0xF)

// Deferred VTX consolidation state (ZAPD-style MergeConnectingVertexLists).
// ZAPD merges VTX per-DList (each DList has its own vertices map and merge pass).
// We collect VTX during each DList parse call and flush at the end of that parse.
namespace DeferredVtx {

static bool sDeferred = false;
static std::vector<PendingVtx> sPendingList;

void BeginDefer() {
    sDeferred = true;
    sPendingList.clear();
}

bool IsDeferred() {
    return sDeferred;
}

std::vector<PendingVtx> SaveAndClearPending() {
    auto saved = std::move(sPendingList);
    sPendingList.clear();
    return saved;
}

void RestorePending(std::vector<PendingVtx>& saved) {
    // Prepend saved items to current pending list (in case anything was added during the save)
    saved.insert(saved.end(), sPendingList.begin(), sPendingList.end());
    sPendingList = std::move(saved);
}

void AddPending(uint32_t addr, uint32_t count) {
    sPendingList.push_back({addr, count});
}

// Flush pending VTX for a single DList: merge adjacent arrays and register assets.
// Called at the end of each DList parse() to match ZAPD's per-DList merge scope.
void FlushDeferred(const std::string& baseName) {
    // Don't clear sDeferred here — it stays active for the entire room.
    // Each DList parse flushes its own collected VTX.
    auto pending = std::move(sPendingList);
    sPendingList.clear();

    if (pending.empty()) {
        return;
    }

    SPDLOG_INFO("VTX FlushDeferred: {} pending VTX for {}", pending.size(), baseName);

    // Sort by segment offset
    std::sort(pending.begin(), pending.end(),
        [](const PendingVtx& a, const PendingVtx& b) {
            return SEGMENT_OFFSET(a.addr) < SEGMENT_OFFSET(b.addr);
        });

    // Merge adjacent/overlapping VTX arrays (ZAPD's MergeConnectingVertexLists algorithm).
    // Two arrays merge if the first array's end >= the second's start.
    struct MergedVtx {
        uint32_t addr;      // segment address of start
        uint32_t endOff;    // segment offset of end (exclusive)
    };
    std::vector<MergedVtx> merged;

    for (auto& pv : pending) {
        uint32_t startOff = SEGMENT_OFFSET(pv.addr);
        uint32_t endOff = startOff + pv.count * sizeof(N64Vtx_t);

        if (merged.empty() || startOff > merged.back().endOff) {
            // New group
            merged.push_back({pv.addr, endOff});
        } else {
            // Extend existing group
            if (endOff > merged.back().endOff) {
                merged.back().endOff = endOff;
            }
        }
    }

    // Register each merged VTX group as an asset
    for (auto& mg : merged) {
        uint32_t startOff = SEGMENT_OFFSET(mg.addr);
        uint32_t totalBytes = mg.endOff - startOff;
        uint32_t totalCount = totalBytes / sizeof(N64Vtx_t);

        // Build proper symbol: baseName + "Vtx_" + 6-digit hex offset
        std::ostringstream ss;
        ss << baseName << "Vtx_" << std::uppercase << std::hex
           << std::setfill('0') << std::setw(6) << startOff;
        std::string symbol = ss.str();

        // Use OOT:ARRAY type to match reference O2R format (ResourceType::Array)
        YAML::Node vtx;
        vtx["type"] = "OOT:ARRAY";
        vtx["offset"] = mg.addr;
        vtx["count"] = totalCount;
        vtx["symbol"] = symbol;
        vtx["array_type"] = "VTX";

        SPDLOG_INFO("VTX consolidation: {} at 0x{:X} count={}", symbol, mg.addr, totalCount);
        Companion::Instance->AddAsset(vtx);

        // Register overlap mappings for all pending addresses within this group.
        // Use the symbol (not the full path) to match existing SearchVtx-based overlaps,
        // since the export path applies RelativePath() which prepends the directory.
        auto registeredNode = Companion::Instance->GetNodeByAddr(mg.addr);
        if (registeredNode.has_value()) {
            auto [fullPath, vtxNode] = registeredNode.value();
            auto overlapTuple = std::make_tuple(symbol, vtxNode);
            for (auto& pv : pending) {
                uint32_t pvOff = SEGMENT_OFFSET(pv.addr);
                if (pvOff > startOff && pvOff < mg.endOff) {
                    GFXDOverride::RegisterVTXOverlap(pv.addr, overlapTuple);
                }
            }
        }
    }
}

void EndDefer() {
    sDeferred = false;
    sPendingList.clear();
}

} // namespace DeferredVtx

// Try to remap a segmented address to a segment where the asset can be found.
// OoT uses segment 8 to reference textures in the same file (loaded at segment 6).
// When segments share the same ROM offset, remap the address to the primary segment.
// If expectedType is provided, only return a match if the asset has that type.
static uint32_t RemapSegmentedAddr(uint32_t addr, const std::string& expectedType = "") {
    uint8_t seg = SEGMENT_NUMBER(addr);
    uint32_t offset = SEGMENT_OFFSET(addr);
    auto segBase = Companion::Instance->GetFileOffsetFromSegmentedAddr(seg);
    if (!segBase.has_value()) return addr;

    // Check all other segments for one that shares the same ROM base
    // and where the asset is actually registered with the expected type
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

std::unordered_map<std::string, uint8_t> gF3DTable = {
    { "G_VTX", 0x04 },     { "G_DL", 0x06 },      { "G_MTX", 0x1 },    { "G_ENDDL", 0xB8 },
    { "G_SETTIMG", 0xFD }, { "G_MOVEMEM", 0x03 }, { "G_MV_L0", 0x86 }, { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA }, { "G_TRI2", 0xB1 },    { "G_QUAD", -1 },
    { "G_BRANCH_Z", 0xB0 }, { "G_RDPHALF_1", 0xB4 }
};

std::unordered_map<std::string, uint8_t> gF3DExTable = {
    { "G_VTX", 0x04 },     { "G_DL", 0x06 },      { "G_MTX", 0x1 },    { "G_ENDDL", 0xB8 },
    { "G_SETTIMG", 0xFD }, { "G_MOVEMEM", 0x03 }, { "G_MV_L0", 0x86 }, { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA }, { "G_TRI2", 0xB1 },    { "G_QUAD", 0xB5 },
    { "G_BRANCH_Z", 0xB0 }, { "G_RDPHALF_1", 0xB4 }
};

std::unordered_map<std::string, uint8_t> gF3DEx2Table = {
    { "G_VTX", 0x01 },     { "G_DL", 0xDE },      { "G_MTX", 0xDA },   { "G_ENDDL", 0xDF },
    { "G_SETTIMG", 0xFD }, { "G_MOVEMEM", 0xDC }, { "G_MV_L0", 0x86 }, { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA }, { "G_TRI2", 0x06 },    { "G_QUAD", 0x07 },
    { "G_BRANCH_Z", 0x04 }, { "G_RDPHALF_1", 0xE1 }
};

std::unordered_map<GBIVersion, std::unordered_map<std::string, uint8_t>> gGBITable = {
    { GBIVersion::f3d, gF3DTable },
    { GBIVersion::f3dex, gF3DExTable },
    { GBIVersion::f3dex2, gF3DEx2Table },
};

#define GBI(cmd) gGBITable[Companion::Instance->GetGBIVersion()][#cmd]

#ifdef STANDALONE
void GFXDSetGBIVersion() {
    switch (Companion::Instance->GetGBIVersion()) {
        case GBIVersion::f3d:
            gfxd_target(gfxd_f3d);
            break;
        case GBIVersion::f3dex:
            gfxd_target(gfxd_f3dex);
            break;
        case GBIVersion::f3db:
            gfxd_target(gfxd_f3db);
            break;
        case GBIVersion::f3dex2:
            gfxd_target(gfxd_f3dex2);
            break;
        case GBIVersion::f3dexb:
            gfxd_target(gfxd_f3dexb);
            break;
    }
}
#endif

ExportResult DListHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Gfx " << symbol << "[];\n";
    return std::nullopt;
}

#ifdef STANDALONE
bool hasTable = false;
ExportResult DListCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                       YAML::Node& node, std::string* replacement) {
    const auto cmds = std::static_pointer_cast<DListData>(raw)->mGfxs;
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto searchTable = Companion::Instance->SearchTable(offset);
    const auto sz = (sizeof(uint32_t) * cmds.size());
    hasTable = searchTable.has_value();

    size_t isize = cmds.size();

    char out[0xFFFF] = { 0 };

    gfxd_input_buffer(cmds.data(), sizeof(uint32_t) * cmds.size());
    gfxd_output_buffer(out, sizeof(out));

    gfxd_endian(gfxd_endian_host, sizeof(uint32_t));
    gfxd_macro_fn([] {
        auto gfx = static_cast<const N64Gfx*>(gfxd_macro_data());
        const uint8_t opcode = (gfx->words.w0 >> 24) & 0xFF;

        if (hasTable) {
            gfxd_puts(fourSpaceTab fourSpaceTab);
        } else {
            gfxd_puts(fourSpaceTab);
        }

        // For mk64 only
        if (opcode == GBI(G_QUAD) && Companion::Instance->GetGBIMinorVersion() == GBIMinorVersion::Mk64) {
            GFXDOverride::Quadrangle(gfx);
            // Prevents mix and matching of quadrangle commands. Forces 2TRI only.
        } else if (opcode == GBI(G_TRI2)) {
            GFXDOverride::Triangle2(gfx);
        } else {
            gfxd_macro_dflt();
        }

        gfxd_puts(",\n");
        return 0;
    });

    gfxd_vtx_callback(GFXDOverride::Vtx);
    gfxd_timg_callback(GFXDOverride::Texture);
    gfxd_dl_callback(GFXDOverride::DisplayList);
    gfxd_tlut_callback(GFXDOverride::Palette);
    gfxd_lightsn_callback(GFXDOverride::Lights);
    gfxd_light_callback(GFXDOverride::Light);
    gfxd_vp_callback(GFXDOverride::Viewport);
    gfxd_mtx_callback(GFXDOverride::Matrix);
    GFXDSetGBIVersion();

    if (searchTable.has_value()) {
        const auto [name, start, end, mode, index_size] = searchTable.value();

        if (mode != TableMode::Append) {
            throw std::runtime_error("Reference mode is not supported for now");
        }

        if (index_size > -1) {
            isize = index_size;
        }

        if (start == offset) {
            gfxd_puts(("Gfx " + name + "[][" + std::to_string(isize / 2) + "] = {\n").c_str());
            gfxd_puts("\t{\n");
        } else {
            gfxd_puts("\t{\n");
        }
        gfxd_execute();
        write << std::string(out);
        if (end == offset) {
            write << fourSpaceTab << "}\n";
            write << "};\n";
            if (Companion::Instance->IsDebug()) {
                write << "// count: " << std::to_string(sz / 8) << " Gfx\n";
            } else {
                write << "\n";
            }
        } else {
            write << fourSpaceTab << "},\n";
        }
    } else {
        gfxd_puts(("Gfx " + symbol + "[] = {\n").c_str());
        gfxd_execute();
        write << std::string(out);
        write << "};\n";

        if (Companion::Instance->IsDebug()) {
            write << "// count: " << std::to_string(sz / 8) << " Gfx\n";
        } else {
            write << "\n";
        }
    }

    return offset + sz;
}

void DebugDisplayList(uint32_t w0, uint32_t w1) {
    uint32_t dlist[] = { w0, w1 };
    gfxd_input_buffer(dlist, sizeof(dlist));
    gfxd_output_fd(fileno(stdout));
    gfxd_endian(gfxd_endian_host, sizeof(uint32_t));
    gfxd_macro_fn([]() {
        gfxd_puts("> ");
        gfxd_macro_dflt();
        gfxd_puts("\n");
        return 0;
    });
    gfxd_vtx_callback(GFXDOverride::Vtx);
    gfxd_timg_callback(GFXDOverride::Texture);
    gfxd_dl_callback(GFXDOverride::DisplayList);
    gfxd_tlut_callback(GFXDOverride::Palette);
    // gfxd_light_callback(GFXDOverride::Light);
    GFXDSetGBIVersion();
    gfxd_execute();
}
#endif

std::optional<std::tuple<std::string, YAML::Node>> SearchVtx(uint32_t ptr) {
    // Search both VTX and OOT:ARRAY types for vertex data
    std::vector<std::string> vtxTypes = {"VTX", "OOT:ARRAY"};

    // Translate ptr to absolute ROM address for cross-segment comparison.
    // Use GetFileOffsetFromSegmentedAddr to avoid error logging from TranslateAddr.
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

        if (!decs.has_value()) {
            continue;
        }

        for (auto& dec : decs.value()) {
            auto [name, node] = dec;

            // For OOT:ARRAY, only match VTX array_type
            if (type == "OOT:ARRAY") {
                auto arrayType = GetSafeNode<std::string>(node, "array_type", "");
                if (arrayType != "VTX") {
                    continue;
                }
            }

            auto offset = GetSafeNode<uint32_t>(node, "offset");
            auto count = GetSafeNode<uint32_t>(node, "count");
            auto end = ALIGN16((count * sizeof(N64Vtx_t)));

            // Compare in absolute ROM address space to handle cross-segment references
            uint32_t absOffset = offset;
            if (IS_SEGMENTED(offset)) {
                auto seg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(offset));
                if (!seg.has_value()) {
                    continue;
                }
                absOffset = seg.value() + SEGMENT_OFFSET(offset);
            }

            if (absPtr > absOffset && absPtr < absOffset + end) {
                return std::make_tuple(GetSafeNode<std::string>(node, "symbol", name), node);
            }
        }
    }

    return std::nullopt;
}

ExportResult DListBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    const auto gbi = Companion::Instance->GetGBIVersion();
    auto cmds = std::static_pointer_cast<DListData>(raw)->mGfxs;
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, Torch::ResourceType::DisplayList, 0);

    writer.Write((int8_t)gbi);

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

        // gSunDL VTX override: must run before normal G_VTX handler.
        // OTRExporter's GetDeclarationRanged finds a texture covering this VTX address,
        // using it with a byte offset. Torch auto-discovers a VTX at the exact address instead.
        if (opcode == GBI(G_VTX) && replacement && replacement->find("gSunDL") != std::string::npos) {
            auto ptr = w1;
            std::optional<std::pair<std::string, uint32_t>> rangedMatch;
            // Search non-VTX types only: the VTX at this address is auto-discovered by Torch
            // but doesn't exist in OTRExporter. We want the texture/blob that covers this range.
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
                        // Use GetSafeStringByAddr to get the same path format as SETTIMG resolution
                        auto path = Companion::Instance->GetSafeStringByAddr(doffset, type);
                        uint32_t diff = ASSET_PTR(ptr) - ASSET_PTR(doffset);
                        if (path.has_value() && (!rangedMatch.has_value() || diff < rangedMatch->second)) {
                            rangedMatch = std::make_pair(path.value(), diff);
                        }
                    }
                }
            }
            if (rangedMatch.has_value()) {
                auto& [path, diff] = rangedMatch.value();
                uint64_t hash = CRC64(path.c_str());
                size_t nvtx2 = C0(12, 8);
                size_t didx2 = C0(1, 7) - C0(12, 8);
                N64Gfx value = gsSPVertexOTR(diff, nvtx2, didx2);
                writer.Write(value.words.w0);
                writer.Write(value.words.w1);
                w0 = hash >> 32;
                w1 = hash & 0xFFFFFFFF;
                writer.Write(w0);
                writer.Write(w1);
                continue;
            }
        }

        if (opcode == GBI(G_VTX)) {
            size_t nvtx;
            size_t didx;

            switch (gbi) {
                case GBIVersion::f3dex2:
                    nvtx = C0(12, 8);
                    didx = C0(1, 7) - C0(12, 8);
                    break;
                case GBIVersion::f3dex:
                case GBIVersion::f3dexb:
                    nvtx = C0(10, 6);
                    didx = C0(17, 7);
                    break;
                default:
                    nvtx = (C0(0, 16)) / sizeof(N64Vtx_t);
                    didx = C0(16, 4);
                    break;
            }

            auto ptr = Companion::Instance->PatchVirtualAddr(w1);

            // Check if VTX is on an alias segment (e.g. segment 8 aliasing segment 6).
            // OTRExporter's HasSegment returns false for alias segments → fallback encoding.
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
                    isAliasSegment = true;  // unconfigured segment
                }
            }

            if (isAliasSegment) {
                // OTRExporter fallback: gsSPVertex((addr & 0xFFFFFFFF) + 1, nn, didx)
                // Preserves segment bits in w1.
                w1 = w1 + 1;
                SPDLOG_INFO("VTX export: alias segment for 0x{:X}", ptr);
            } else if (auto overlap = GFXDOverride::GetVtxOverlap(ptr); overlap.has_value()) {
                auto ovnode = std::get<1>(overlap.value());
                auto path = Companion::Instance->RelativePath(std::get<0>(overlap.value()));

                // Check if the VTX is from a different file than the current DList.
                // OTRExporter's GetDeclarationRanged returns nullptr for cross-file VTX,
                // writing a "null vtxDecl" format: w0=G_VTX_OTR_HASH<<24, w1=0 (8 bytes).
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
                    auto count = GetSafeNode<uint32_t>(ovnode, "count");
                    auto diff = ASSET_PTR(ptr) - ASSET_PTR(offset);

                    N64Gfx value = gsSPVertexOTR(diff, nvtx, didx);

                    SPDLOG_INFO("gsSPVertexOTR({}, {}, {})", diff, nvtx, didx);

                    w0 = value.words.w0;
                    w1 = value.words.w1;

                    writer.Write(w0);
                    writer.Write(w1);

                    w0 = hash >> 32;
                    w1 = hash & 0xFFFFFFFF;
                }
            } else {
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
                    // Check if the VTX is from a different file than the current DList.
                    // OTRExporter's GetDeclarationRanged returns nullptr for cross-file VTX,
                    // writing a "null vtxDecl" format: w0=G_VTX_OTR_HASH<<24, w1=0 (8 bytes).
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

                        SPDLOG_INFO("gsSPVertex({}, {}, 0x{:X})", nvtx, didx, ptr);

                        w0 = value.words.w0;
                        w1 = value.words.w1;

                        writer.Write(w0);
                        writer.Write(w1);

                        w0 = hash >> 32;
                        w1 = hash & 0xFFFFFFFF;
                    }
                } else if (IS_VIRTUAL_SEGMENT(w1)) {
                    // OTRExporter's HasSegment returns true for segment 0x80 but
                    // GetDeclarationRanged fails → null vtxDecl encoding.
                    // This happens when a code-section DList references VTX via
                    // virtual addresses that don't map to any declared asset
                    // (e.g. sCircleDList in z_fbdemo_circle).
                    w0 = G_VTX_OTR_HASH << 24;
                    w1 = 0;
                } else {
                    SPDLOG_WARN("VTX export: NOT FOUND vtx at 0x{:X} w1=0x{:X} replacement={}", ptr, w1, *replacement);
                    // Cross-segment VTX: modify w1 in place, no explicit write.
                    // OTRExporter re-encodes via gsSPVertex with (address & 0xFFFFFFFF) + 1.
                    // The final write handles output (single command, no duplication).
                    w1 = (w1 & 0x0FFFFFFF) + 1;
                }
            }
        }

        if (opcode == GBI(G_DL)) {
            auto ptr = w1;
            uint8_t dlSeg = SEGMENT_NUMBER(ptr);

            // Segments 8-13 are runtime-swapped (eye/mouth textures, limb DLists)
            // and must stay unresolved, matching OTRExporter's !HasSegment behavior.
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
                w1 = 0;  // OTRExporter always writes w1=0 for G_DL_OTR_HASH header

                writer.Write(w0);
                writer.Write(w1);

                w0 = hash >> 32;
                w1 = hash & 0xFFFFFFFF;

                if (branch) {
                    writer.Write(w0);
                    writer.Write(w1);

                    value = gsSPRawOpcode(GBI(G_ENDDL));
                    w0 = value.words.w0;
                    w1 = value.words.w1;
                }
            } else {
                SPDLOG_WARN("Could not find display list at 0x{:X}", ptr);
                // Cross-segment DL: modify w0/w1 in place, no explicit write.
                // OTRExporter re-encodes via gsSPDisplayList with (address & 0x0FFFFFFF) + 1.
                // The final write handles output (single command, no duplication).
                w1 = (w1 & 0x0FFFFFFF) + 1;
            }
        }

        // TODO: Fix this opcode
        if (opcode == GBI(G_MOVEMEM)) {
            auto ptr = w1;

            uint8_t index = 0;
            uint8_t offset = 0;
            bool hasOffset = false;

            switch (gbi) {
                case GBIVersion::f3d:
                    index = C0(16, 8);
                    offset = 0;
                    break;
                case GBIVersion::f3dex:
                    index = (w0 >> 16) & 0xFF;
                    offset = C0(8, 8) * 8;
                    break;
                default:
                    index = C0(0, 8);
                    offset = C0(8, 8) * 8;
                    break;
            }

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
        }

        if (opcode == GBI(G_SETTIMG)) {
            auto ptr = w1;
            // Do NOT use RemapSegmentedAddr for textures: segments 8-13 are used for
            // runtime-swapped textures (eyes, mouth) and must stay unresolved.
            // OTRExporter's HasSegment check returns false for these segments.
            auto dec = Companion::Instance->GetSafeStringByAddr(ptr, "TEXTURE");

            if (dec.has_value()) {
                uint64_t hash = CRC64(dec.value().c_str());

                if (hash == 0) {
                    throw std::runtime_error("Texture hash is 0 for " + dec.value());
                }

                SPDLOG_INFO("Found texture: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, dec.value());

                if (Companion::Instance->GetGBIMinorVersion() == GBIMinorVersion::PM64) {
                    uint32_t newW0 = (G_SETTIMG_OTR_HASH << 24) | (w0 & 0x00FFFFFF);
                    writer.Write(newW0);
                    writer.Write(ptr);
                } else {
                    uint32_t newW0 = (G_SETTIMG_OTR_HASH << 24) | (w0 & 0x00FFFFFF);
                    writer.Write(newW0);
                    writer.Write(static_cast<uint32_t>(0));
                }

                w0 = hash >> 32;
                w1 = hash & 0xFFFFFFFF;
            } else {
                SPDLOG_WARN("Could not find texture at 0x{:X}", ptr);
                // Texture not found: write original G_SETTIMG with address+1.
                // OTRExporter writes this explicitly then falls through to write again,
                // producing a duplicate command. We match that behavior for compatibility.
                // OTRExporter hack: sShadowMaterialDL (ovl_En_Jsjutan) references a BSS texture.
                // The actor loads it into segment 0xC at runtime.
                if (replacement && replacement->find("sShadowMaterialDL") != std::string::npos) {
                    w1 = 0x0C000001;
                } else {
                    auto patchedPtr = Companion::Instance->PatchVirtualAddr(ptr);
                    w1 = (patchedPtr & 0x0FFFFFFF) + 1;
                }
                writer.Write(w0);
                writer.Write(w1);
            }
        }

        // OTRExporter hack: gSunDL textures are I4 but the DList commands use IA16 parameters.
        // Adjust G_SETTILE size and G_LOADBLOCK texel count to match the actual texture format.
        // See OTRExporter DisplayListExporter.cpp G_SETTILE/G_LOADBLOCK cases.
        if (replacement && replacement->find("gSunDL") != std::string::npos) {
            constexpr uint8_t G_SETTILE_OP = 0xF5;
            constexpr uint8_t G_LOADBLOCK_OP = 0xF3;
            constexpr uint8_t G_TX_LOADTILE = 7;
            constexpr uint8_t G_IM_SIZ_4b = 0;

            if (opcode == G_SETTILE_OP) {
                uint8_t tile = (w1 >> 24) & 0x07;
                if (tile != G_TX_LOADTILE) {
                    // Force size field (bits 19-20 of w0) to G_IM_SIZ_4b
                    w0 = (w0 & ~(0x3 << 19)) | (G_IM_SIZ_4b << 19);
                }
            }

            if (opcode == G_LOADBLOCK_OP) {
                // G_LOADBLOCK encoding: w0 = [op:8][uls:12][ult:12], w1 = [tile:4][texels:12][dxt:12]
                // OTRExporter checks ult (w0 bits 0-11), not the tile index from w1.
                uint32_t ult = w0 & 0xFFF;
                uint32_t texels = (w1 >> 12) & 0xFFF;
                if (ult != G_TX_LOADTILE) {
                    texels = (texels + 1) / 2 - 1;
                    w1 = (w1 & ~(0xFFF << 12)) | ((texels & 0xFFF) << 12);
                }
            }

        }

        if (opcode == GBI(G_MTX)) {
            auto ptr = w1;
            auto dec = Companion::Instance->GetSafeStringByAddr(ptr, "MTX");
            if (!dec.has_value()) {
                auto remapped = RemapSegmentedAddr(ptr, "MTX");
                if (remapped != ptr) {
                    dec = Companion::Instance->GetSafeStringByAddr(remapped, "MTX");
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
                writer.Write(w1);  // Keep original segmented address

                w0 = hash >> 32;
                w1 = hash & 0xFFFFFFFF;
            } else {
                SPDLOG_WARN("Could not find matrix at 0x{:X}", ptr);
                // Cross-segment matrix: modify w1 in place, no explicit write
                // OTRExporter re-encodes via gsSPMatrix which preserves w0 and sets
                // w1 = (address & 0x0FFFFFFF) + 1. The final write handles output.
                w1 = (w1 & 0x0FFFFFFF) + 1;
            }
        }

        // G_RDPHALF_1 + G_BRANCH_Z pair: OTRExporter converts G_BRANCH_Z to
        // G_BRANCH_Z_OTR (16 bytes: header + DL hash) and writes G_NOOP for G_RDPHALF_1.
        // The DL address is taken from G_RDPHALF_1's w1.
        if (opcode == GBI(G_RDPHALF_1) && i + 2 < cmds.size()) {
            uint8_t nextOpcode = cmds[i + 2] >> 24;
            if (nextOpcode == GBI(G_BRANCH_Z)) {
                // Write G_NOOP for G_RDPHALF_1 (OTRExporter zeroes this out)
                w0 = 0;
                w1 = 0;
            }
        }

        if (opcode == GBI(G_BRANCH_Z)) {
            // Get DL address from previous G_RDPHALF_1 command
            uint32_t dlAddr = (i >= 2) ? cmds[i - 1] : 0;
            auto dec = Companion::Instance->GetSafeStringByAddr(dlAddr, "GFX");
            if (!dec.has_value()) {
                auto remapped = RemapSegmentedAddr(dlAddr, "GFX");
                if (remapped != dlAddr) {
                    dec = Companion::Instance->GetSafeStringByAddr(remapped, "GFX");
                }
            }

            if (dec.has_value()) {
                uint64_t hash = CRC64(dec.value().c_str());

                // Write G_BRANCH_Z_OTR header
                uint32_t a = (w0 >> 12) & 0xFFF;
                uint32_t b = w0 & 0xFFF;
                uint32_t branchW0 = (G_BRANCH_Z_OTR << 24) | _SHIFTL(a, 12, 12) | _SHIFTL(b, 0, 12);
                uint32_t branchW1 = w1; // z-value
                writer.Write(branchW0);
                writer.Write(branchW1);

                w0 = hash >> 32;
                w1 = hash & 0xFFFFFFFF;
            } else {
                SPDLOG_WARN("Could not find display list for G_BRANCH_Z at 0x{:X}", dlAddr);
            }
        }

        // G_SETOTHERMODE_H texture LUT re-encoding (matches OTRExporter behavior)
        // In F3DEX2: G_SETOTHERMODE_H = 0xE3
        // OTRExporter special-cases sft==14 (G_MDSFT_TEXTLUT) by un-shifting the data value
        if (opcode == 0xE3 && gbi == GBIVersion::f3dex2) {
            uint8_t ss = (w0 >> 8) & 0xFF;
            uint8_t nn = w0 & 0xFF;
            int32_t sft = 32 - (nn + 1) - ss;

            if (sft == 14) {
                w1 = w1 >> 14;
            }
        }

        // OTRExporter re-encodes G_NOOP via gsDPNoOp() which always produces {0, 0}.
        if (opcode == 0x00) {
            w0 = 0;
            w1 = 0;
        }

        // HACK: OTRExporter's default case for unhandled opcodes writes
        // opcode<<24, 0 (zeroing lower bits and w1). This matters when junk data
        // gets parsed as DList commands (e.g. sCircleDList's offset points into VTX data).
        // Zero out opcodes that OTRExporter doesn't explicitly handle.
        if (gbi == GBIVersion::f3dex2) {
            static const std::unordered_set<uint8_t> otrHandledOpcodes = {
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // G_NOOP..G_QUAD
                0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDE, 0xDF, // G_TEXTURE..G_ENDDL
                0xE1, 0xE2, 0xE3, 0xE4,                         // G_RDPHALF_1..G_TEXRECT
                0xE6, 0xE7, 0xE8, 0xE9, 0xEF,                   // syncs + G_RDPSETOTHERMODE
                0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5,             // G_LOADTLUT..G_SETTILE
                0xFA, 0xFB, 0xFC, 0xFD,                         // G_SETPRIMCOLOR..G_SETTIMG
            };
            if (otrHandledOpcodes.find(opcode) == otrHandledOpcodes.end()) {
                w0 = (uint32_t)opcode << 24;
                w1 = 0;
            }
        }

        writer.Write(w0);
        writer.Write(w1);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> DListFactory::parse(std::vector<uint8_t>& raw_buffer, YAML::Node& node) {
    const auto gbi = Companion::Instance->GetGBIVersion();

    auto count = GetSafeNode<int32_t>(node, "count", -1);
    auto isAutogen = GetSafeNode<bool>(node, "autogen", false);
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

        if (opcode == GBI(G_ENDDL)) {
            processing = false;
        }

        if (opcode == GBI(G_DL)) {
            if (C0(16, 1) == G_DL_NO_PUSH) {
                SPDLOG_INFO("Branch List Command Found");
                processing = false;
            }

            if (SEGMENT_NUMBER(node["offset"].as<uint32_t>()) == SEGMENT_NUMBER(w1)) {
                YAML::Node gfx;
                gfx["type"] = "GFX";
                gfx["offset"] = w1;

                // Derive child DList symbol from parent's naming convention.
                // If parent is "Bmori1_room_0DL_005C98", extract "Bmori1_room_0DL_"
                // and apply it to the child offset to get "Bmori1_room_0DL_001CB0".
                auto parentSymbol = GetSafeNode<std::string>(node, "symbol", "");
                auto dlPos = parentSymbol.rfind("DL_");
                if (dlPos != std::string::npos) {
                    auto base = parentSymbol.substr(0, dlPos + 3); // "Bmori1_room_0DL_"
                    uint32_t childOffset = SEGMENT_OFFSET(w1);
                    std::ostringstream ss;
                    ss << base << std::uppercase << std::hex
                       << std::setfill('0') << std::setw(6) << childOffset;
                    gfx["symbol"] = ss.str();
                }

                // Save/restore pending VTX around AddAsset: it may trigger immediate
                // parsing of the child DList, whose FlushDeferred would consume the
                // parent's pending VTX. Each DList should have its own VTX scope.
                auto savedDL = DeferredVtx::IsDeferred()
                    ? DeferredVtx::SaveAndClearPending()
                    : std::vector<DeferredVtx::PendingVtx>{};
                Companion::Instance->AddAsset(gfx);
                if (DeferredVtx::IsDeferred()) {
                    DeferredVtx::RestorePending(savedDL);
                }
            }
        }

        // This opcode is generally used as part of multiple macros such as gsSPSetLights1.
        // We need to process gsSPLight which is a subcommand inside G_MOVEMEM (0x03).
        if (opcode == GBI(G_MOVEMEM)) {
            // 0x03860000 or 0x03880000 subcommand will contain 0x86/0x88 for G_MV_L0 and G_MV_L1. Other subcommands
            // also exist.
            uint8_t subcommand = (w0 >> 16) & 0xFF;
            uint8_t index = 0;
            uint8_t offset = 0;
            bool light = false;

            switch (Companion::Instance->GetGBIVersion()) {
                // If needing light generation on G_MV_L0 then we'll need to walk the DL ptr forward/backward to check
                // for 0xBC Otherwise mk64 will break. PD: Mega, this works for sm64 too, why you didn't implement it?
                // >:( PD: Im jk, <3
                case GBIVersion::f3d:
                case GBIVersion::f3dex:
                    /*
                     * Only generate lights on the second gsSPLight.
                     * gsSPSetLights1(name) outputs three macros:
                     *
                     * gsSPNumLights(NUMLIGHTS_1)
                     * gsSPLight(&name.l[0], G_MV_L0)
                     * gsSPLight(&name.a, G_MV_L1) <-- This ptr is used to generate the lights
                     */
                    if (subcommand == GBI(G_MV_L1)) {
                        light = true;
                    }
                case GBIVersion::f3dex2:
                    index = C0(0, 8);
                    offset = C0(8, 8) * 8;

                    // same thing as above; see macro gSPLight at gbi.h
                    if (index == GBI(G_MV_LIGHT) && offset == (2 * 24 + 24)) {
                        light = true;
                    }
                    break;
                default:
                    throw std::runtime_error("Unsupported GBI version");
            }

            if (light) {
                YAML::Node lnode;
                lnode["type"] = "LIGHTS";
                lnode["offset"] = w1;
                Companion::Instance->AddAsset(lnode);
            }
        }

        // Auto-discover G_BRANCH_Z target DLists.
        // G_RDPHALF_1 stores the branch target DList address in w1,
        // followed by G_BRANCH_Z. Register the target as a GFX asset.
        // Also scan the branch target's VTX commands for consolidation with the parent DList.
        // In ZAPD, branch target DLists share the parent's vertices map, so adjacent VTX
        // from parent and branch target get merged. We replicate this by adding the branch
        // target's VTX to the parent's deferred set before FlushDeferred runs.
        if (opcode == GBI(G_RDPHALF_1)) {
            if (IS_SEGMENTED(w1) && SEGMENT_NUMBER(w1) == SEGMENT_NUMBER(node["offset"].as<uint32_t>())) {
                const auto decl = Companion::Instance->GetNodeByAddr(w1);
                if (!decl.has_value()) {
                    auto parentSymbol = GetSafeNode<std::string>(node, "symbol", "");
                    auto dlPos = parentSymbol.rfind("DL_");
                    if (dlPos != std::string::npos) {
                        YAML::Node gfx;
                        gfx["type"] = "GFX";
                        gfx["offset"] = w1;
                        auto base = parentSymbol.substr(0, dlPos + 3);
                        uint32_t childOffset = SEGMENT_OFFSET(w1);
                        std::ostringstream ss;
                        ss << base << std::uppercase << std::hex
                           << std::setfill('0') << std::setw(6) << childOffset;
                        gfx["symbol"] = ss.str();

                        // Save parent's pending VTX before AddAsset: AddAsset triggers
                        // immediate parsing of the branch target DList, whose FlushDeferred
                        // would consume/clear the parent's pending VTX.
                        auto savedPending = DeferredVtx::IsDeferred()
                            ? DeferredVtx::SaveAndClearPending()
                            : std::vector<DeferredVtx::PendingVtx>{};
                        Companion::Instance->AddAsset(gfx);
                        if (DeferredVtx::IsDeferred()) {
                            DeferredVtx::RestorePending(savedPending);
                        }
                    }
                }

                // Scan branch target DList for G_VTX to include in parent's deferred VTX set
                if (DeferredVtx::IsDeferred()) {
                    YAML::Node branchNode;
                    branchNode["offset"] = w1;
                    auto [__, branchSeg] = Decompressor::AutoDecode(branchNode, raw_buffer);
                    LUS::BinaryReader branchReader(branchSeg.data, branchSeg.size);
                    branchReader.SetEndianness(Torch::Endianness::Big);

                    while (branchReader.GetBaseAddress() + 8 <= branchSeg.size) {
                        auto bw0 = branchReader.ReadUInt32();
                        auto bw1 = branchReader.ReadUInt32();
                        uint8_t bOpcode = bw0 >> 24;

                        if (bOpcode == GBI(G_ENDDL)) break;

                        if (bOpcode == GBI(G_VTX) && IS_SEGMENTED(bw1)) {
                            uint32_t bNvtx;
                            switch (gbi) {
                                case GBIVersion::f3dex2:
                                    bNvtx = (bw0 >> 12) & 0xFF;
                                    break;
                                case GBIVersion::f3dex:
                                case GBIVersion::f3dexb:
                                    bNvtx = (bw0 >> 10) & 0x3F;
                                    break;
                                default:
                                    bNvtx = ((bw0 & 0xFFFF)) / sizeof(N64Vtx_t);
                                    break;
                            }
                            DeferredVtx::AddPending(bw1, bNvtx);
                        }
                    }
                }
            }
        }

        // Auto-discover matrix assets referenced by G_MTX.
        // OTRExporter extracts these from the DList and emits them as separate resources.
        // ZAPD names them "{dlistName}Mtx_000000" (rawDataIndex = 0 relative to the matrix).
        if (opcode == GBI(G_MTX)) {
            if (IS_SEGMENTED(w1) && SEGMENT_NUMBER(w1) == SEGMENT_NUMBER(node["offset"].as<uint32_t>())) {
                const auto decl = Companion::Instance->GetNodeByAddr(w1);
                if (!decl.has_value()) {
                    auto parentSymbol = GetSafeNode<std::string>(node, "symbol", "");

                    YAML::Node mtxNode;
                    mtxNode["type"] = "MTX";
                    mtxNode["offset"] = w1;
                    mtxNode["symbol"] = parentSymbol + "Mtx_000000";
                    Companion::Instance->AddAsset(mtxNode);
                }
            }
        }

        if (opcode == GBI(G_VTX)) {
            uint32_t nvtx;

            switch (gbi) {
                case GBIVersion::f3dex2:
                    nvtx = C0(12, 8);
                    break;
                case GBIVersion::f3dex:
                case GBIVersion::f3dexb:
                    nvtx = C0(10, 6);
                    break;
                default:
                    nvtx = (C0(0, 16)) / sizeof(N64Vtx_t);
                    break;
            }
            const auto decl = Companion::Instance->GetNodeByAddr(w1);

            if (!decl.has_value()) {
                auto adjPtr = Companion::Instance->PatchVirtualAddr(w1);
                auto search = SearchVtx(adjPtr);

                if (search.has_value()) {
                    auto [path, vtx] = search.value();

                    SPDLOG_INFO("Path: {}", path);

                    auto lOffset = GetSafeNode<uint32_t>(vtx, "offset");
                    auto lCount = GetSafeNode<uint32_t>(vtx, "count");
                    auto lSize = ALIGN16(lCount * sizeof(N64Vtx_t));

                    // Compare in absolute ROM address space for cross-segment support
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
                        // Register with the patched address (used by binary exporter lookup)
                        GFXDOverride::RegisterVTXOverlap(adjPtr, search.value());
                    }
                } else {
                    // Skip VTX auto-creation for:
                    // 1. Unresolved virtual addresses (0x80XXXXXX) — no VRAM mapping
                    // 2. Alias segments (e.g. segment 8 aliasing segment 6) —
                    //    OTRExporter's HasSegment returns false for alias segments
                    // 3. Unconfigured segments — AutoDecode would crash
                    bool skipVtx = !IS_SEGMENTED(adjPtr);
                    if (!skipVtx) {
                        auto thisSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(adjPtr));
                        if (thisSeg.has_value()) {
                            for (uint8_t s = 0; s < SEGMENT_NUMBER(adjPtr); s++) {
                                auto otherSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(s);
                                if (otherSeg.has_value() && otherSeg.value() == thisSeg.value()) {
                                    skipVtx = true;
                                    break;
                                }
                            }
                        } else {
                            skipVtx = true;
                        }
                    }

                    if (!skipVtx) {
                        if (DeferredVtx::IsDeferred()) {
                            DeferredVtx::AddPending(adjPtr, nvtx);
                        } else {
                            YAML::Node vtx;
                            vtx["type"] = "VTX";
                            vtx["offset"] = adjPtr;
                            vtx["count"] = nvtx;
                            Companion::Instance->AddAsset(vtx);
                        }
                    }
                }
            } else {
                SPDLOG_WARN("Found vtx at 0x{:X}", w1);
            }
        }

        if (count != -1 && length++ >= count) {
            break;
        }

        gfxs.push_back(w0);
        gfxs.push_back(w1);
    }

    // Flush per-DList VTX: merge adjacent arrays discovered during this parse.
    // ZAPD merges VTX per-DList (each DList has its own vertices map).
    if (DeferredVtx::IsDeferred()) {
        auto symbol = GetSafeNode<std::string>(node, "symbol", "");
        // Extract base name: "Bmori1_room_0DL_005C98" -> "Bmori1_room_0"
        auto dlPos = symbol.rfind("DL_");
        std::string baseName = (dlPos != std::string::npos) ? symbol.substr(0, dlPos) : symbol;
        DeferredVtx::FlushDeferred(baseName);
    }

    return std::make_shared<DListData>(gfxs);
}
