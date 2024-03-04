#include "DisplayListOverrides.h"

#include "utils/Decompressor.h"
#include "DisplayListFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

#include <gfxd.h>
#include <string>

namespace GFXDOverride {

std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> gVtxOverlapMap;

void Triangle2(const Gfx* gfx) {
    auto w0 = gfx->words.w0;
    auto w1 = gfx->words.w1;

    auto v1 = std::to_string( ((w0 >> 16) & 0xFF) / 2 );
    auto v2 = std::to_string( ((w0 >> 8) & 0xFF) / 2 );
    auto v3 = std::to_string( (w0 & 0xFF) / 2 );

    auto v4 = std::to_string( ((w1 >> 16) & 0xFF) / 2 );
    auto v5 = std::to_string( ((w1 >> 8) & 0xFF) / 2 );
    auto v6 = std::to_string( (w1 & 0xFF) / 2 );
    auto flag = "0";

    const auto str = v1 + ", " + v2 + ", " + v3 + ", " + flag + ", " + v4 + ", " + v5 + ", " + v6 + ", " + flag;

    gfxd_puts("gsSP2Triangles(");
    gfxd_puts(str.c_str());
    gfxd_puts(")");
}

void Quadrangle(const Gfx* gfx) {
    auto w1 = gfx->words.w1;

    auto v1 = std::to_string( ((w1 >> 16) & 0xFF) / 2 );
    auto v2 = std::to_string( ((w1 >> 8) & 0xFF) / 2 );
    auto v3 = std::to_string( (w1 & 0xFF) / 2 );
    auto v4 = std::to_string( ((w1 >> 24) & 0xFF) / 2 );
    auto flag = "0";

    const auto str = v1 + ", " + v2 + ", " + v3 + ", " + v4 + ", " + flag;

    gfxd_puts("gsSP1Quadrangle(");
    gfxd_puts(str.c_str());
    gfxd_puts(")");
}

int Vtx(uint32_t ptr, int32_t num) {
    auto overlap = VtxOverlapping(ptr);
    auto isOverlapping = overlap.has_value();
    SPDLOG_INFO("Vtx: 0x{:X} Num: {} Overlapping: {}", ptr, num, isOverlapping);
    auto dec = Companion::Instance->GetNodeByAddr(isOverlapping ? overlap.value().first : ptr);

    if(dec.has_value()){
        auto node = std::get<1>(dec.value());
        auto symbol = GetSafeNode<std::string>(node, "symbol");
        SPDLOG_INFO("Found Vtx: 0x{:X} Symbol: {}", ptr, symbol);
        if(isOverlapping){
            gfxd_puts("&");
            gfxd_puts(symbol.c_str());
            gfxd_puts("[");
            gfxd_puts(std::to_string(overlap.value().second).c_str());
            gfxd_puts("]");
            return 1;
        } else {
            gfxd_puts(symbol.c_str());
            return 1;
        }
    }

    SPDLOG_WARN("Could not find vtx at 0x{:X}", ptr);
    return 0;
}

int Texture(uint32_t ptr, int32_t fmt, int32_t siz, int32_t width, int32_t height, int32_t pal) {
    auto dec = Companion::Instance->GetNodeByAddr(ptr);

    if(dec.has_value()){
        auto node = std::get<1>(dec.value());
        auto symbol = GetSafeNode<std::string>(node, "symbol");
        SPDLOG_INFO("Found Texture: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Could not find texture at 0x{:X}", ptr);
    return 0;
}

int Palette(uint32_t ptr, int32_t idx, int32_t count) {
    auto dec = Companion::Instance->GetNodeByAddr(ptr);

    if(dec.has_value()){
        auto node = std::get<1>(dec.value());
        auto symbol = GetSafeNode<std::string>(node, "symbol");
        SPDLOG_INFO("Found TLUT: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Could not find tlut at 0x{:X}", ptr);
    return 0;
}

int Light(uint32_t ptr, int32_t count) {
    auto dec = Companion::Instance->GetNodeByAddr(ptr);

    if(dec.has_value()){
        auto node = std::get<1>(dec.value());
        auto symbol = GetSafeNode<std::string>(node, "symbol");
        SPDLOG_INFO("Found Light: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Could not find light at 0x{:X}", ptr);
    return 0;
}

int DisplayList(uint32_t ptr) {
    auto dec = Companion::Instance->GetNodeByAddr(ptr);

    if(dec.has_value()){
        auto node = std::get<1>(dec.value());
        auto symbol = GetSafeNode<std::string>(node, "symbol");
        SPDLOG_INFO("Found Display List: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Could not find display list to override at 0x{:X}", ptr);
    return 0;
}

bool SearchVTXOverlaps(uint32_t ptr, uint32_t num) {
    for(auto& [key, value] : gVtxOverlapMap){
        auto [addr, count] = value;
        if(ptr > addr && ptr <= (addr + (count * sizeof(Vtx_t)))){
            SPDLOG_INFO("Found Vtx Overlap: 0x{:X} Overlaps with 0x{:X} Count: {}", ptr, addr, count);
            return true;
        }
    }

    SPDLOG_INFO("Vtx Overlap Not Found: 0x{:X} Count: {}", ptr, num);

    gVtxOverlapMap[ptr] = std::make_pair(ptr, num);
    return false;
}

void ClearVtx() {
    gVtxOverlapMap.clear();
}

std::optional<std::pair<uint32_t, uint32_t>> VtxOverlapping(uint32_t ptr) {
    for(auto& [key, value] : gVtxOverlapMap){
        auto [addr, count] = value;
        if(ptr > addr && ptr < addr + (count * sizeof(Vtx_t))){
            auto diff = ptr - addr;
            return std::make_pair(addr, diff / sizeof(Vtx_t));
        }
    }

    return std::nullopt;
}
}
