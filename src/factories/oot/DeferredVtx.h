#pragma once

#ifdef OOT_SUPPORT

#include <cstdint>
#include <string>
#include <vector>

namespace DeferredVtx {
    struct PendingVtx { uint32_t addr; uint32_t count; };
    void BeginDefer();
    bool IsDeferred();
    void FlushDeferred(const std::string& baseName);
    std::vector<PendingVtx> SaveAndClearPending();
    void RestorePending(std::vector<PendingVtx>& saved);
    void AddPending(uint32_t addr, uint32_t count);
    void EndDefer();
}

#endif
