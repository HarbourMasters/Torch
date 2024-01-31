#include "DisplayListOverrides.h"

#include "utils/Decompressor.h"
#include "DisplayListFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

#include <gfxd.h>
#include <string>

namespace GFXDOverride {

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

int Vtx(uint32_t vtx, int32_t num) {
    auto ptr = Decompressor::TranslateAddr(vtx);
    auto dec = Companion::Instance->GetNodeByAddr(ptr);

    if(dec.has_value()){
        auto node = std::get<1>(dec.value());
        auto symbol = GetSafeNode<std::string>(node, "symbol");
        SPDLOG_INFO("Found Vtx: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Could not find vtx at 0x{:X}", ptr);
    return 0;
}

int Texture(uint32_t timg, int32_t fmt, int32_t siz, int32_t width, int32_t height, int32_t pal) {
    auto ptr = Decompressor::TranslateAddr(timg);
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

int Light(uint32_t lightsn, int32_t count) {
    auto ptr = Decompressor::TranslateAddr(lightsn);
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

int DisplayList(uint32_t dl) {
    auto ptr = Decompressor::TranslateAddr(dl);
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
}
