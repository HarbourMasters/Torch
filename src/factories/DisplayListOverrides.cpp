#include "DisplayListOverrides.h"

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
    uint32_t ptr = SEGMENT_OFFSET(vtx);
    if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); decl.has_value()){
        auto node = std::get<1>(decl.value());
        auto symbol = node["symbol"].as<std::string>();
        SPDLOG_INFO("Wrote vtx: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Warning: Could not find vtx at 0x{:X}", ptr);
    return 0;
}

int Texture(uint32_t timg, int32_t fmt, int32_t siz, int32_t width, int32_t height, int32_t pal) {
    uint32_t ptr = SEGMENT_OFFSET(timg);
    if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); decl.has_value()){
        auto node = std::get<1>(decl.value());
        auto symbol = node["symbol"].as<std::string>();
        SPDLOG_INFO("Wrote texture: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Warning: Could not find texture at 0x{:X}", ptr);
    return 0;
}

int Light(uint32_t lightsn, int32_t count) {
    uint32_t ptr = SEGMENT_OFFSET(lightsn);
    if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); decl.has_value()){
        auto node = std::get<1>(decl.value());
        auto symbol = node["symbol"].as<std::string>();
        SPDLOG_INFO("Wrote light: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Warning: Could not find light at 0x{:X}", ptr);
    return 0;
}

int DisplayList(uint32_t dl) {
    uint32_t ptr = SEGMENT_OFFSET(dl);
    if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); decl.has_value()){
        auto node = std::get<1>(decl.value());
        auto symbol = node["symbol"].as<std::string>();
        SPDLOG_INFO("Wrote display list: 0x{:X} Symbol: {}", ptr, symbol);
        gfxd_puts(symbol.c_str());
        return 1;
    }

    SPDLOG_WARN("Warning: Could not find display list to override at 0x{:X}", ptr);
    return 0;
}
}
