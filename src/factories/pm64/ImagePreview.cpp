#include "ImagePreview.h"

#ifdef BUILD_UI

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Companion.h"
#include "imgui.h"
#include "types/RawBuffer.h"
#include "ui/BaseBackend.h"
#include "ui/ExportUtils.h"
#include "ui/Widgets.h"

extern "C" {
#include "n64graphics/n64graphics.h"
}

namespace {

uint32_t LE32(const std::vector<uint8_t>& d, uint64_t off) {
    if (off + 4 > d.size()) {
        return 0;
    }
    uint32_t v;
    std::memcpy(&v, d.data() + off, 4);
    return v;
}

uint16_t LE16(const std::vector<uint8_t>& d, uint64_t off) {
    if (off + 2 > d.size()) {
        return 0;
    }
    uint16_t v;
    std::memcpy(&v, d.data() + off, 2);
    return v;
}

// Palettes stay big-endian RGBA5551 in every parsed PM64 blob.
void Put5551BE(std::vector<uint8_t>& out, const uint8_t* p) {
    const uint16_t c = (uint16_t)((p[0] << 8) | p[1]);
    out.push_back((uint8_t)(((c >> 11) & 31) * 255 / 31));
    out.push_back((uint8_t)(((c >> 6) & 31) * 255 / 31));
    out.push_back((uint8_t)(((c >> 1) & 31) * 255 / 31));
    out.push_back((c & 1) ? 255 : 0);
}

// fmt/siz are G_IM_FMT_* / G_IM_SIZ_* values; pal points at big-endian RGBA5551.
std::vector<uint8_t> DecodeN64(const uint8_t* raster, const uint8_t* pal, int fmt, int siz, int w, int h) {
    std::vector<uint8_t> out;
    if (w <= 0 || h <= 0) {
        return out;
    }
    out.reserve((size_t)w * h * 4);
    const int count = w * h;
    for (int i = 0; i < count; ++i) {
        switch (fmt) {
            case 0: // RGBA
                if (siz == 3) {
                    out.insert(out.end(), raster + i * 4, raster + i * 4 + 4);
                } else {
                    Put5551BE(out, raster + i * 2);
                }
                break;
            case 2: { // CI
                const int idx = siz == 1 ? raster[i] : (i % 2 == 0 ? raster[i / 2] >> 4 : raster[i / 2] & 0xF);
                Put5551BE(out, pal + idx * 2);
                break;
            }
            case 3: { // IA
                uint8_t in, a;
                if (siz == 2) {
                    in = raster[i * 2];
                    a = raster[i * 2 + 1];
                } else if (siz == 1) {
                    in = (uint8_t)(raster[i] & 0xF0);
                    in |= in >> 4;
                    a = (uint8_t)((raster[i] & 0xF) * 17);
                } else {
                    const uint8_t n = i % 2 == 0 ? raster[i / 2] >> 4 : raster[i / 2] & 0xF;
                    in = (uint8_t)(((n >> 1) & 7) * 255 / 7);
                    a = (n & 1) ? 255 : 0;
                }
                out.push_back(in);
                out.push_back(in);
                out.push_back(in);
                out.push_back(a);
                break;
            }
            case 4: { // I
                const uint8_t in = siz == 1 ? raster[i]
                                            : (uint8_t)((i % 2 == 0 ? raster[i / 2] >> 4 : raster[i / 2] & 0xF) * 17);
                out.push_back(in);
                out.push_back(in);
                out.push_back(in);
                out.push_back(in);
                break;
            }
            default:
                out.push_back(255);
                out.push_back(0);
                out.push_back(255);
                out.push_back(255);
                break;
        }
    }
    return out;
}

std::map<std::string, UI::TextureHandle>& TextureCache() {
    static std::map<std::string, UI::TextureHandle> sCache;
    return sCache;
}

const UI::TextureHandle* FindCached(const std::string& key) {
    const auto it = TextureCache().find(key);
    return it != TextureCache().end() ? &it->second : nullptr;
}

UI::TextureHandle UploadCached(const std::string& key, const std::vector<uint8_t>& rgba, int w, int h) {
    const auto it = TextureCache().find(key);
    if (it != TextureCache().end()) {
        return it->second;
    }
    UI::TextureHandle handle = UI::kInvalidTexture;
    if (!rgba.empty() && rgba.size() >= (size_t)w * h * 4 && UI::GetBackend() != nullptr) {
        handle = UI::GetBackend()->UploadRGBA8(rgba.data(), w, h);
    }
    TextureCache()[key] = handle;
    return handle;
}

void DrawCheckerboard(ImDrawList* dl, const ImVec2& min, const ImVec2& size) {
    constexpr float kCell = 8.0f;
    dl->AddRectFilled(min, min + size, IM_COL32(48, 48, 52, 255));
    for (float y = 0; y < size.y; y += kCell) {
        for (float x = 0; x < size.x; x += kCell) {
            if ((int)((x + y) / kCell) % 2 == 0) {
                continue;
            }
            const ImVec2 c0 = min + ImVec2(x, y);
            const ImVec2 c1 = c0 + ImVec2(std::min(kCell, size.x - x), std::min(kCell, size.y - y));
            dl->AddRectFilled(c0, c1, IM_COL32(64, 64, 70, 255));
        }
    }
}

// Checkerboard-backed thumbnail with a zoomed hover tooltip.
void DrawThumb(UI::TextureHandle handle, int w, int h, float side, const char* info) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 box(side, side);
    DrawCheckerboard(dl, origin, box);
    if (handle != UI::kInvalidTexture && w > 0 && h > 0) {
        const float aspect = (float)w / (float)h;
        ImVec2 fit = box;
        if (aspect >= 1.0f) {
            fit.y = side / aspect;
        } else {
            fit.x = side * aspect;
        }
        ImGui::SetCursorScreenPos(origin + ImVec2((side - fit.x) * 0.5f, (side - fit.y) * 0.5f));
        ImGui::Image(handle, fit);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            const float big = std::min(384.0f, std::max((float)w, (float)h) * 3.0f);
            ImVec2 bigFit(big, big);
            if (aspect >= 1.0f) {
                bigFit.y = big / aspect;
            } else {
                bigFit.x = big * aspect;
            }
            ImGui::Image(handle, bigFit);
            if (info != nullptr) {
                ImGui::TextUnformatted(info);
            }
            ImGui::EndTooltip();
        }
    }
    dl->AddRect(origin, origin + box, IM_COL32(255, 255, 255, 40), 4.0f);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(box);
}

bool WritePng(const std::filesystem::path& path, const std::vector<uint8_t>& pixels, int w, int h) {
    if (pixels.empty()) {
        return false;
    }
    unsigned char* png = nullptr;
    int size = 0;
    if (rgba2png(&png, &size, (const rgba*)pixels.data(), w, h) == 0 || png == nullptr) {
        return false;
    }
    FILE* f = fopen(path.string().c_str(), "wb");
    bool ok = false;
    if (f != nullptr) {
        fwrite(png, 1, size, f);
        fclose(f);
        ok = true;
    }
    free(png);
    return ok;
}

void ExportPngButton(const std::string& assetName, const std::vector<uint8_t>& pixels, int w, int h) {
    if (ImGui::SmallButton("PNG##imgexp")) {
        const auto path = UI::ExportFilePath(assetName, "png");
        UI::NoteExport(assetName, WritePng(path, pixels, w, h) ? path.string() : "export failed");
    }
    UI::DrawExportMarker(assetName);
}

} // namespace

float PM64StoryImageFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() + 148.0f;
}

void PM64StoryImageFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    auto& node = const_cast<YAML::Node&>(item.node);
    const auto& d = std::static_pointer_cast<RawBuffer>(item.data.value())->mBuffer;
    const int w = (int)GetSafeNode<uint32_t>(node, "width");
    const int h = (int)GetSafeNode<uint32_t>(node, "height");
    const bool hasPal = GetSafeNode<bool>(node, "has_palette");

    static std::map<std::string, std::vector<uint8_t>> sPixels;
    auto pit = sPixels.find(item.name);
    if (pit == sPixels.end()) {
        const uint8_t* pal = hasPal ? d.data() + (size_t)w * h : nullptr;
        pit = sPixels.emplace(item.name, DecodeN64(d.data(), pal, hasPal ? 2 : 3, 1, w, h)).first;
    }
    const UI::TextureHandle handle = UploadCached(item.name, pit->second, w, h);

    ImGui::BeginGroup();
    DrawThumb(handle, w, h, 128.0f, hasPal ? "CI8" : "IA8");
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("story image  \xe2\x80\x94  %dx%d %s", w, h, hasPal ? "CI8 + palette" : "IA8");
    ExportPngButton(item.name, pit->second, w, h);
    ImGui::EndGroup();
}

namespace {

struct TitleLayout {
    int w, h;
    int fmt, siz;
    const char* label;
};

// TITLE_DATA blobs have no per-image header; dimensions come from the game
// code (title screen) or from the known raster sizes (partner portraits).
bool TitleLayoutFor(const std::string& symbol, size_t size, TitleLayout& out) {
    static const std::map<std::string, TitleLayout> kKnown = {
        { "title_logo", { 200, 112, 0, 3, "RGBA32" } },
        { "title_press_start", { 128, 32, 3, 1, "IA8" } },
        { "title_copyright", { 144, 32, 3, 1, "IA8" } },
    };
    const auto it = kKnown.find(symbol);
    if (it != kKnown.end()) {
        out = it->second;
        return true;
    }
    static const std::map<size_t, TitleLayout> kBySize = {
        { 15750, { 150, 105, 2, 1, "CI8" } }, // partner portraits
    };
    const auto sit = kBySize.find(size);
    if (sit != kBySize.end()) {
        out = sit->second;
        return true;
    }
    return false;
}

// Sibling "<name>_pal" asset holding the big-endian RGBA5551 palette.
const std::shared_ptr<RawBuffer> FindSiblingPalette(const std::string& name) {
    const std::string palName = name + "_pal";
    for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
        for (const auto& r : results) {
            if (r.name == palName && r.data.has_value()) {
                return std::static_pointer_cast<RawBuffer>(r.data.value());
            }
        }
    }
    return nullptr;
}

bool IsPaletteAsset(const std::string& name, size_t size) {
    return name.size() > 4 && name.compare(name.size() - 4, 4, "_pal") == 0 && (size == 0x200 || size == 0x20);
}

} // namespace

float PM64TitleDataFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() + 148.0f;
}

void PM64TitleDataFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    auto& node = const_cast<YAML::Node&>(item.node);
    const auto& d = std::static_pointer_cast<RawBuffer>(item.data.value())->mBuffer;
    const auto symbol = GetSafeNode<std::string>(node, "symbol", item.name);

    static std::map<std::string, std::vector<uint8_t>> sPixels;

    // Palette blobs render as a swatch grid.
    if (IsPaletteAsset(item.name, d.size())) {
        const int colors = (int)d.size() / 2;
        const int w = 16, h = colors / 16 > 0 ? colors / 16 : 1;
        auto pit = sPixels.find(item.name);
        if (pit == sPixels.end()) {
            std::vector<uint8_t> rgba;
            for (int i = 0; i < colors; ++i) {
                Put5551BE(rgba, d.data() + i * 2);
            }
            pit = sPixels.emplace(item.name, std::move(rgba)).first;
        }
        const UI::TextureHandle handle = UploadCached(item.name, pit->second, w, h);
        ImGui::BeginGroup();
        DrawThumb(handle, w, h, 128.0f, "RGBA5551 palette");
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("palette  \xe2\x80\x94  %d colors", colors);
        ExportPngButton(item.name, pit->second, w, h);
        ImGui::EndGroup();
        return;
    }

    TitleLayout layout{};
    if (!TitleLayoutFor(symbol, d.size(), layout)) {
        ImGui::TextDisabled("image  \xe2\x80\x94  %zu bytes (unknown layout)", d.size());
        return;
    }
    auto pit = sPixels.find(item.name);
    if (pit == sPixels.end()) {
        const uint8_t* pal = nullptr;
        std::shared_ptr<RawBuffer> palBuf;
        if (layout.fmt == 2) {
            palBuf = FindSiblingPalette(item.name);
            if (palBuf == nullptr || palBuf->mBuffer.size() < 0x200) {
                ImGui::TextDisabled("image  \xe2\x80\x94  CI8 with no sibling palette");
                return;
            }
            pal = palBuf->mBuffer.data();
        }
        pit = sPixels.emplace(item.name, DecodeN64(d.data(), pal, layout.fmt, layout.siz, layout.w, layout.h)).first;
    }
    const UI::TextureHandle handle = UploadCached(item.name, pit->second, layout.w, layout.h);

    ImGui::BeginGroup();
    DrawThumb(handle, layout.w, layout.h, 128.0f, layout.label);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("image  \xe2\x80\x94  %dx%d %s, %zu bytes", layout.w, layout.h, layout.label, d.size());
    ExportPngButton(item.name, pit->second, layout.w, layout.h);
    ImGui::EndGroup();
}

namespace {

struct MapTexEntry {
    std::string name;
    uint32_t rasterOff = 0, palOff = 0;
    uint16_t w = 0, h = 0;
    uint8_t fmt = 0, siz = 0, extra = 0;
};

uint32_t MapTexRasterSize(uint16_t w, uint16_t h, uint8_t siz, uint8_t extra) {
    uint32_t size = (uint32_t)w * h;
    if (extra == 1) {
        const int minW[4] = { 16, 8, 4, 2 };
        for (int div = 2; w / div >= minW[siz & 3] && h / div > 0; div *= 2) {
            size += (uint32_t)(w / div) * (h / div);
        }
    }
    switch (siz) {
        case 0: return size / 2;
        case 2: return size * 2;
        case 3: return size * 4;
        default: return size;
    }
}

// Same walk as the shape viewer's tex archive indexing (parsed blob is LE with
// main fields in the upper nibbles).
std::vector<MapTexEntry> WalkMapTexArchive(const std::vector<uint8_t>& d) {
    std::vector<MapTexEntry> entries;
    uint32_t off = 0;
    while (off + 0x30 <= d.size() && entries.size() < 512) {
        MapTexEntry e;
        for (int i = 0; i < 32 && d[off + i] != 0; ++i) {
            e.name.push_back((char)d[off + i]);
        }
        if (e.name.empty()) {
            break;
        }
        const uint16_t auxW = LE16(d, off + 0x20), mainW = LE16(d, off + 0x22);
        const uint16_t auxH = LE16(d, off + 0x24), mainH = LE16(d, off + 0x26);
        e.extra = d[off + 0x29];
        e.fmt = d[off + 0x2B] >> 4;
        e.siz = d[off + 0x2C] >> 4;
        if (mainW == 0 || mainH == 0 || mainW > 1024 || mainH > 1024) {
            break;
        }
        const uint32_t rasterSize = MapTexRasterSize(mainW, mainH, e.siz, e.extra);
        const uint32_t palSize = e.fmt == 2 ? (e.siz == 1 ? 0x200 : 0x20) : 0;
        uint32_t auxRaster = 0, auxPal = 0;
        if (e.extra == 3) {
            const uint8_t auxFmt = d[off + 0x2B] & 0xF, auxSiz = d[off + 0x2C] & 0xF;
            auxRaster = MapTexRasterSize(auxW, auxH, auxSiz, 0);
            auxPal = auxFmt == 2 ? (auxSiz == 1 ? 0x200 : 0x20) : 0;
        }
        e.rasterOff = off + 0x30;
        e.palOff = e.rasterOff + rasterSize;
        e.w = mainW;
        e.h = e.extra == 2 ? (uint16_t)(mainH / 2) : mainH;
        if ((uint64_t)e.rasterOff + rasterSize + palSize <= d.size()) {
            entries.push_back(e);
        }
        off += 0x30 + rasterSize + palSize + auxRaster + auxPal;
    }
    return entries;
}

} // namespace

float PM64MapTextureFactoryUI::GetItemHeight(const ParseResultData&) {
    return 420.0f;
}

void PM64MapTextureFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto buf = std::static_pointer_cast<RawBuffer>(item.data.value());
    static std::map<std::string, std::vector<MapTexEntry>> sEntries;
    auto eit = sEntries.find(item.name);
    if (eit == sEntries.end()) {
        eit = sEntries.emplace(item.name, WalkMapTexArchive(buf->mBuffer)).first;
    }
    const auto& entries = eit->second;
    ImGui::TextDisabled("texture archive  \xe2\x80\x94  %zu textures", entries.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("PNG all##maptex")) {
        int written = 0;
        for (const auto& e : entries) {
            const auto& d = buf->mBuffer;
            const auto pixels =
                DecodeN64(d.data() + e.rasterOff, e.fmt == 2 ? d.data() + e.palOff : nullptr, e.fmt, e.siz, e.w, e.h);
            if (WritePng(UI::ExportFilePath(item.name + "/" + e.name, "png"), pixels, e.w, e.h)) {
                written++;
            }
        }
        UI::NoteExport(item.name, std::to_string(written) + " png -> torch-exports/" + item.name + "/");
    }
    UI::DrawExportMarker(item.name);

    ImGui::BeginChild("##maptexgrid", ImVec2(0, 360.0f), false);
    const float avail = ImGui::GetContentRegionAvail().x;
    constexpr float kThumb = 64.0f;
    const int cols = std::max(1, (int)(avail / (kThumb + 10.0f)));
    int col = 0;
    for (const auto& e : entries) {
        if (col != 0) {
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        const std::string key = item.name + "/" + e.name;
        const auto& d = buf->mBuffer;
        const UI::TextureHandle* cached = FindCached(key);
        const UI::TextureHandle handle =
            cached != nullptr
                ? *cached
                : UploadCached(key,
                               DecodeN64(d.data() + e.rasterOff, e.fmt == 2 ? d.data() + e.palOff : nullptr, e.fmt,
                                         e.siz, e.w, e.h),
                               e.w, e.h);
        char info[96];
        static const char* kFmt[] = { "RGBA", "YUV", "CI", "IA", "I" };
        snprintf(info, sizeof(info), "%s  %ux%u %s%d%s", e.name.c_str(), e.w, e.h,
                 e.fmt < 5 ? kFmt[e.fmt] : "?", 4 << e.siz, e.extra == 2 || e.extra == 3 ? " +aux" : "");
        DrawThumb(handle, e.w, e.h, kThumb, info);
        ImGui::EndGroup();
        col = (col + 1) % cols;
    }
    ImGui::EndChild();
}

namespace {

struct SpriteRaster {
    uint32_t imgOff = 0;
    uint8_t w = 0, h = 0;
    int8_t palette = 0;
};

struct SpriteInfo {
    std::vector<SpriteRaster> rasters;
    std::vector<uint32_t> palettes; // offsets to 16-color RGBA5551 (BE)
    int animCount = 0;
};

uint32_t BE32(const std::vector<uint8_t>& d, uint64_t off) {
    if (off + 4 > d.size()) {
        return 0;
    }
    return ((uint32_t)d[off] << 24) | ((uint32_t)d[off + 1] << 16) | ((uint32_t)d[off + 2] << 8) | d[off + 3];
}

// Player sprite rasters live in a shared blob streamed through descriptors
// (spr_get_player_raster): desc = size/16 in the top 3 nibbles, segment
// offset in the low 5; the header gives the image data segment base.
struct PlayerRasters {
    std::vector<uint8_t> image;
    std::vector<uint32_t> sets;  // per-sprite descriptor index ranges
    std::vector<uint32_t> descs;
    uint32_t imageBase = 0;
    bool valid = false;
};

const std::shared_ptr<RawBuffer> FindBlob(const char* suffix) {
    for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
        for (const auto& r : results) {
            if (r.data.has_value() && r.name.size() >= strlen(suffix) &&
                r.name.compare(r.name.size() - strlen(suffix), strlen(suffix), suffix) == 0) {
                return std::static_pointer_cast<RawBuffer>(r.data.value());
            }
        }
    }
    return nullptr;
}

const PlayerRasters& GetPlayerRasters() {
    static PlayerRasters sShared;
    static bool sInit = false;
    if (sInit) {
        return sShared;
    }
    sInit = true;
    const auto header = FindBlob("player_raster_header");
    const auto sets = FindBlob("player_raster_sets");
    const auto descs = FindBlob("player_raster_load_descriptors");
    const auto image = FindBlob("player_raster_image_data");
    if (header == nullptr || sets == nullptr || descs == nullptr || image == nullptr) {
        return sShared;
    }
    sShared.imageBase = BE32(header->mBuffer, 8);
    for (uint64_t o = 0; o + 4 <= sets->mBuffer.size(); o += 4) {
        sShared.sets.push_back(BE32(sets->mBuffer, o));
    }
    for (uint64_t o = 0; o + 4 <= descs->mBuffer.size(); o += 4) {
        sShared.descs.push_back(BE32(descs->mBuffer, o));
    }
    sShared.image = image->mBuffer;
    sShared.valid = !sShared.sets.empty() && !sShared.descs.empty();
    return sShared;
}

// player_sprite_N asset name -> N, or -1 for NPC sprites.
int PlayerSpriteIndex(const std::string& name) {
    const auto pos = name.rfind("player_sprite_");
    if (pos == std::string::npos) {
        return -1;
    }
    return atoi(name.c_str() + pos + strlen("player_sprite_"));
}

SpriteInfo WalkSprite(const std::vector<uint8_t>& d) {
    SpriteInfo out;
    const uint32_t rastersOff = LE32(d, 0);
    const uint32_t palettesOff = LE32(d, 4);
    for (uint32_t p = 0x10; p + 4 <= d.size(); p += 4) {
        const uint32_t v = LE32(d, p);
        if (v == 0xFFFFFFFF) {
            break;
        }
        out.animCount++;
    }
    for (uint32_t p = palettesOff; p + 4 <= d.size() && out.palettes.size() < 64; p += 4) {
        const uint32_t v = LE32(d, p);
        if (v == 0xFFFFFFFF) {
            break;
        }
        if (v + 32 <= d.size()) {
            out.palettes.push_back(v);
        }
    }
    for (uint32_t p = rastersOff; p + 4 <= d.size() && out.rasters.size() < 512; p += 4) {
        const uint32_t v = LE32(d, p);
        if (v == 0xFFFFFFFF) {
            break;
        }
        if (v + 8 > d.size()) {
            continue;
        }
        SpriteRaster r;
        r.imgOff = LE32(d, v);
        r.w = d[v + 4];
        r.h = d[v + 5];
        r.palette = (int8_t)d[v + 6];
        if (r.w > 0 && r.h > 0) {
            out.rasters.push_back(r);
        }
    }
    return out;
}

// Raster pixels for one sprite frame; player sprites read the shared blob.
const uint8_t* SpriteRasterPixels(const std::vector<uint8_t>& d, const SpriteRaster& r, int playerIdx,
                                  size_t rasterIndex) {
    const size_t bytes = (size_t)r.w * r.h / 2;
    if (playerIdx >= 0) {
        const PlayerRasters& shared = GetPlayerRasters();
        if (!shared.valid || (size_t)playerIdx + 1 >= shared.sets.size()) {
            return nullptr;
        }
        const uint32_t begin = shared.sets[playerIdx];
        if (begin + rasterIndex >= shared.descs.size()) {
            return nullptr;
        }
        const uint32_t desc = shared.descs[begin + rasterIndex];
        const int64_t off = (int64_t)(desc & 0xFFFFF) - shared.imageBase;
        if (off < 0 || (uint64_t)off + bytes > shared.image.size()) {
            return nullptr;
        }
        return shared.image.data() + off;
    }
    if ((uint64_t)r.imgOff + bytes > d.size()) {
        return nullptr;
    }
    return d.data() + r.imgOff;
}

} // namespace

float PM64SpriteFactoryUI::GetItemHeight(const ParseResultData&) {
    return 420.0f;
}

void PM64SpriteFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto buf = std::static_pointer_cast<RawBuffer>(item.data.value());
    const auto& d = buf->mBuffer;
    static std::map<std::string, SpriteInfo> sInfo;
    auto iit = sInfo.find(item.name);
    if (iit == sInfo.end()) {
        iit = sInfo.emplace(item.name, WalkSprite(d)).first;
    }
    const SpriteInfo& info = iit->second;

    static std::map<std::string, int> sPalChoice;
    int& palChoice = sPalChoice.try_emplace(item.name, -1).first->second;
    ImGui::TextDisabled("sprite  \xe2\x80\x94  %zu rasters, %zu palettes, %d animations", info.rasters.size(),
                        info.palettes.size(), info.animCount);
    ImGui::SetNextItemWidth(150.0f);
    const int playerIdx = PlayerSpriteIndex(item.name);
    if (ImGui::SmallButton("PNG all##sprite") && !info.palettes.empty()) {
        int written = 0;
        for (size_t i = 0; i < info.rasters.size(); ++i) {
            const SpriteRaster& r = info.rasters[i];
            int pal = palChoice >= 0 ? palChoice : r.palette;
            if (pal < 0 || pal >= (int)info.palettes.size()) {
                pal = 0;
            }
            const uint8_t* raster = SpriteRasterPixels(d, r, playerIdx, i);
            if (raster == nullptr) {
                continue;
            }
            char frame[32];
            snprintf(frame, sizeof(frame), "/raster_%03zu", i);
            const auto pixels = DecodeN64(raster, d.data() + info.palettes[pal], 2, 0, r.w, r.h);
            if (WritePng(UI::ExportFilePath(item.name + frame, "png"), pixels, r.w, r.h)) {
                written++;
            }
        }
        UI::NoteExport(item.name, std::to_string(written) + " png -> torch-exports/" + item.name + "/");
    }
    UI::DrawExportMarker(item.name);
    ImGui::SameLine();
    const std::string palLabel = palChoice < 0 ? "palette: default" : "palette: " + std::to_string(palChoice);
    if (ImGui::BeginCombo("##spritepal", palLabel.c_str())) {
        if (ImGui::Selectable("default", palChoice < 0)) {
            palChoice = -1;
        }
        for (int i = 0; i < (int)info.palettes.size(); ++i) {
            if (ImGui::Selectable(std::to_string(i).c_str(), palChoice == i)) {
                palChoice = i;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::BeginChild("##spritegrid", ImVec2(0, 320.0f), false);
    const float avail = ImGui::GetContentRegionAvail().x;
    constexpr float kThumb = 56.0f;
    const int cols = std::max(1, (int)(avail / (kThumb + 10.0f)));
    int col = 0;
    for (size_t i = 0; i < info.rasters.size() && !info.palettes.empty(); ++i) {
        const SpriteRaster& r = info.rasters[i];
        int pal = palChoice >= 0 ? palChoice : r.palette;
        if (pal < 0 || pal >= (int)info.palettes.size()) {
            pal = 0;
        }
        if (col != 0) {
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        char key[192];
        snprintf(key, sizeof(key), "%s/%zu/p%d", item.name.c_str(), i, pal);
        const UI::TextureHandle* cached = FindCached(key);
        UI::TextureHandle handle;
        if (cached != nullptr) {
            handle = *cached;
        } else {
            const uint8_t* raster = SpriteRasterPixels(d, r, playerIdx, i);
            handle = UploadCached(
                key, raster != nullptr ? DecodeN64(raster, d.data() + info.palettes[pal], 2, 0, r.w, r.h)
                                       : std::vector<uint8_t>{},
                r.w, r.h);
        }
        char label[64];
        snprintf(label, sizeof(label), "raster %zu  %ux%u CI4 pal %d", i, r.w, r.h, pal);
        DrawThumb(handle, r.w, r.h, kThumb, label);
        ImGui::EndGroup();
        col = (col + 1) % cols;
    }
    ImGui::EndChild();
}

namespace {

struct ImgFXModel {
    int vtxCount = 0;
    int frameCount = 0;
    std::vector<int> tris; // absolute frame-0 vertex indices
    float mn[3] = { 0, 0, 0 };
    float mx[3] = { 0, 0, 0 }; // bounds across every keyframe
};

// Blob layout from PM64ImgFXAnimFactory: LE header, 12-byte vertices
// (frame-major), then f3dex2 Gfx words referencing frame-0 vertex addresses.
ImgFXModel ParseImgFX(const std::vector<uint8_t>& d) {
    ImgFXModel m;
    const uint32_t kfOff = LE32(d, 0x00);
    m.vtxCount = LE16(d, 0x08);
    const int gfxCount = LE16(d, 0x0A);
    m.frameCount = LE16(d, 0x0C);
    if (m.vtxCount <= 0 || m.frameCount <= 0) {
        return m;
    }
    const uint32_t gfxBase = 16 + (uint32_t)m.frameCount * m.vtxCount * 12;
    int slots[64] = {};
    for (int g = 0; g < gfxCount; ++g) {
        const uint32_t w0 = LE32(d, gfxBase + g * 8);
        const uint32_t w1 = LE32(d, gfxBase + g * 8 + 4);
        const uint8_t op = w0 >> 24;
        if (op == 0x01) { // G_VTX
            const int numv = (w0 >> 12) & 0xFF;
            const int vend = (w0 >> 1) & 0x7F;
            const int v0 = vend - numv;
            const int src = (int)((w1 - kfOff) / 12);
            for (int k = 0; k < numv && v0 + k < 64; ++k) {
                slots[v0 + k] = (src + k) % m.vtxCount;
            }
        } else if (op == 0x05) { // G_TRI1
            m.tris.push_back(slots[((w0 >> 16) & 0xFF) / 2]);
            m.tris.push_back(slots[((w0 >> 8) & 0xFF) / 2]);
            m.tris.push_back(slots[(w0 & 0xFF) / 2]);
        } else if (op == 0x06) { // G_TRI2
            m.tris.push_back(slots[((w0 >> 16) & 0xFF) / 2]);
            m.tris.push_back(slots[((w0 >> 8) & 0xFF) / 2]);
            m.tris.push_back(slots[(w0 & 0xFF) / 2]);
            m.tris.push_back(slots[((w1 >> 16) & 0xFF) / 2]);
            m.tris.push_back(slots[((w1 >> 8) & 0xFF) / 2]);
            m.tris.push_back(slots[(w1 & 0xFF) / 2]);
        } else if (op == 0xDF) {
            break;
        }
    }
    // Whole-animation bounds keep the preview camera stable across frames
    // (several effects collapse to a point on their first/last frames).
    bool any = false;
    for (int k = 0; k < m.frameCount * m.vtxCount; ++k) {
        const uint64_t v = 16 + (uint64_t)k * 12;
        const float pos[3] = { (float)(int16_t)LE16(d, v), (float)(int16_t)LE16(d, v + 2),
                               (float)(int16_t)LE16(d, v + 4) };
        for (int a = 0; a < 3; ++a) {
            m.mn[a] = any ? std::min(m.mn[a], pos[a]) : pos[a];
            m.mx[a] = any ? std::max(m.mx[a], pos[a]) : pos[a];
        }
        any = true;
    }
    return m;
}

std::map<std::string, UI::OrbitView> sImgFXViews;

} // namespace

float PM64ImgFXAnimFactoryUI::GetItemHeight(const ParseResultData& item) {
    return 76.0f + UI::PreviewBlockHeight(item.name);
}

void PM64ImgFXAnimFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto buf = std::static_pointer_cast<RawBuffer>(item.data.value());
    const auto& d = buf->mBuffer;
    static std::map<std::string, ImgFXModel> sModels;
    auto mit = sModels.find(item.name);
    if (mit == sModels.end()) {
        mit = sModels.emplace(item.name, ParseImgFX(d)).first;
    }
    const ImgFXModel& m = mit->second;
    ImGui::TextDisabled("imgfx animation  \xe2\x80\x94  %d frames, %d vertices, %zu tris", m.frameCount, m.vtxCount,
                        m.tris.size() / 3);
    if (m.tris.empty()) {
        ImGui::TextDisabled("nothing drawable");
        return;
    }

    struct PlayState {
        bool playing = true;
        float frame = 0.0f;
    };
    static std::map<std::string, PlayState> sPlay;
    PlayState& play = sPlay[item.name];
    if (play.playing) {
        play.frame += ImGui::GetIO().DeltaTime * 15.0f;
        if (play.frame >= (float)m.frameCount) {
            play.frame = 0.0f;
        }
    }
    if (ImGui::SmallButton(play.playing ? "Pause##imgfx" : "Play##imgfx")) {
        play.playing = !play.playing;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderFloat("##imgfxframe", &play.frame, 0.0f, (float)(m.frameCount - 1), "frame %.0f");
    ImGui::SameLine();
    if (ImGui::SmallButton("OBJ##imgfx")) {
        const int f = std::clamp((int)play.frame, 0, m.frameCount - 1);
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "_frame_%02d", f);
        const auto path = UI::ExportFilePath(item.name + suffix, "obj");
        FILE* out = fopen(path.string().c_str(), "w");
        if (out != nullptr) {
            for (int i = 0; i < m.vtxCount; ++i) {
                const uint64_t v = 16 + ((uint64_t)f * m.vtxCount + i) * 12;
                fprintf(out, "v %d %d %d\n", (int16_t)LE16(d, v), (int16_t)LE16(d, v + 2), (int16_t)LE16(d, v + 4));
                fprintf(out, "vt %f %f\n", d[v + 6] / 255.0, 1.0 - d[v + 7] / 255.0);
            }
            for (size_t t = 0; t + 2 < m.tris.size(); t += 3) {
                fprintf(out, "f %d/%d %d/%d %d/%d\n", m.tris[t] + 1, m.tris[t] + 1, m.tris[t + 1] + 1,
                        m.tris[t + 1] + 1, m.tris[t + 2] + 1, m.tris[t + 2] + 1);
            }
            fclose(out);
            UI::NoteExport(item.name, path.string());
        } else {
            UI::NoteExport(item.name, "export failed");
        }
    }
    UI::DrawExportMarker(item.name);

    const int frame = std::clamp((int)play.frame, 0, m.frameCount - 1);
    std::vector<UI::PreviewVertex> tris;
    tris.reserve(m.tris.size() + 6);
    // Invisible degenerate corner triangles pin the camera to the animation's
    // global bounds so per-frame refits don't pump or blank the view.
    for (int corner = 0; corner < 2; ++corner) {
        UI::PreviewVertex pv{};
        pv.position[0] = corner == 0 ? m.mn[0] : m.mx[0];
        pv.position[1] = corner == 0 ? m.mn[1] : m.mx[1];
        pv.position[2] = corner == 0 ? m.mn[2] : m.mx[2];
        pv.color[3] = 0;
        tris.push_back(pv);
        tris.push_back(pv);
        tris.push_back(pv);
    }
    for (const int idx : m.tris) {
        const uint64_t v = 16 + ((uint64_t)frame * m.vtxCount + idx) * 12;
        UI::PreviewVertex pv{};
        pv.position[0] = (float)(int16_t)LE16(d, v);
        pv.position[1] = (float)(int16_t)LE16(d, v + 2);
        pv.position[2] = (float)(int16_t)LE16(d, v + 4);
        // uv as color so the mesh structure reads
        pv.color[0] = (uint8_t)(80 + d[v + 6] / 2);
        pv.color[1] = (uint8_t)(120 + d[v + 7] / 3);
        pv.color[2] = 220;
        pv.color[3] = 255;
        tris.push_back(pv);
    }
    UI::OrbitView& view = sImgFXViews[item.name];
    const UI::PreviewCanvas canvas = UI::BeginResizableCanvas("##imgfxview", item.name, view);
    if (canvas.visible) {
        UI::GetBackend()->DrawTriangles(item.name, tris, canvas.origin, canvas.size, view);
    }
}

#endif // BUILD_UI
