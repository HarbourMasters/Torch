#include "DeferredVtx.h"
#include "Companion.h"
#include "factories/DisplayListOverrides.h"
#include "n64/CommandMacros.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

// N64 vertex size in bytes (matching N64Vtx_t: 3*int16 + uint16 + 2*int16 + 4*uchar = 16)
static constexpr uint32_t kVtxSize = 16;

// Deferred VTX consolidation state (ZAPD-style MergeConnectingVertexLists).
// ZAPD merges VTX per-DList (each DList has its own vertices map and merge pass).
// We collect VTX during each DList parse call and flush at the end of that parse.
namespace DeferredVtx {

bool sDeferred = false;
std::vector<PendingVtx> sPendingList;

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
        uint32_t endOff = startOff + pv.count * kVtxSize;

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
        uint32_t totalCount = totalBytes / kVtxSize;

        // Build proper symbol: baseName + "Vtx_" + 6-digit hex offset
        std::ostringstream ss;
        ss << baseName << "Vtx_" << std::uppercase << std::hex
           << std::setfill('0') << std::setw(6) << startOff;
        std::string symbol = ss.str();

        SPDLOG_INFO("VTX consolidation: {} at 0x{:X} count={}", symbol, mg.addr, totalCount);

        // Look up the pre-declared VTX in YAML (should exist with enrichment)
        auto registeredNode = Companion::Instance->GetNodeByAddr(mg.addr);
        if (!registeredNode.has_value()) {
            SPDLOG_WARN("Undeclared VTX at 0x{:X} — YAML enrichment incomplete", mg.addr);
        }

        // Register overlap mappings for all pending addresses within this group.
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
