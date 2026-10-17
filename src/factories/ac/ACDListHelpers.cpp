#include "ACDListHelpers.h"

#include "Companion.h"
#include "factories/DisplayListFactory.h"
#include "n64/gbi-otr.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <strhash64/StrHash64.h>

#ifdef GX_TEXTURE_SUPPORT
#include "gc/GXTexture.h"
#endif

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace AC {

namespace {

constexpr uint8_t OP_VTX = 0x01;
constexpr uint8_t OP_TRI1 = 0x05;
constexpr uint8_t OP_TRI2 = 0x06;
constexpr uint8_t OP_QUAD = 0x07;
constexpr uint8_t OP_TRIN = 0x09;
constexpr uint8_t OP_TRIN_INDEPEND = 0x0A;
constexpr uint8_t OP_QUADN = 0x0D;
constexpr uint8_t OP_LOADBLOCK_WIDE = 0x47;
constexpr uint8_t OP_SETTILE_DOLPHIN = 0xD2;
constexpr uint8_t OP_MTX = 0xDA;
constexpr uint8_t OP_MOVEMEM = 0xDC;
constexpr uint8_t OP_DL = 0xDE;
constexpr uint8_t OP_ENDDL = 0xDF;
constexpr uint8_t OP_RDPLOADSYNC = 0xE6;
constexpr uint8_t OP_RDPPIPESYNC = 0xE7;
constexpr uint8_t OP_RDPTILESYNC = 0xE8;
constexpr uint8_t OP_LOADTLUT = 0xF0;
constexpr uint8_t OP_SETTILESIZE = 0xF2;
constexpr uint8_t OP_LOADBLOCK = 0xF3;
constexpr uint8_t OP_LOADTILE = 0xF4;
constexpr uint8_t OP_SETTILE = 0xF5;
constexpr uint8_t OP_SETTIMG = 0xFD;

constexpr uint32_t FMT_RGBA = 0;
constexpr uint32_t FMT_CI = 2;
constexpr uint32_t FMT_IA = 3;
constexpr uint32_t FMT_I = 4;
constexpr uint32_t SIZ_4b = 0;
constexpr uint32_t SIZ_8b = 1;
constexpr uint32_t SIZ_16b = 2;
constexpr uint32_t SIZ_32b = 3;

constexpr uint32_t TX_LOADTILE = 7;
constexpr uint32_t TX_WRAP = 0;
constexpr uint32_t TX_MIRROR = 1;
constexpr uint32_t TX_CLAMP = 2;
constexpr uint32_t TX_DXT_FRAC = 11;
constexpr uint32_t TX_LDBLK_MAX_TXL = 0xFFF;
constexpr uint32_t MV_LIGHT = 0x0A;
constexpr uint32_t DOLPHIN_IMAGE_BIT = 1u << 18;
constexpr uint32_t TLUT_DOLPHIN = 2;
constexpr uint32_t MAX_VERTICES = 64;
constexpr uint8_t OP_DL_INDEX = 0x3D;

struct SizInfo {
    uint32_t loadBlock;
    uint32_t incr;
    uint32_t shift;
    uint32_t bytes;
    uint32_t lineBytes;
};

constexpr SizInfo SIZ_INFO[4] = {
    { SIZ_16b, 3, 2, 0, 0 },
    { SIZ_16b, 1, 1, 1, 1 },
    { SIZ_16b, 0, 0, 2, 2 },
    { SIZ_32b, 0, 0, 4, 2 },
};

uint32_t Shift(uint32_t v, uint32_t s, uint32_t w) {
    return (v & ((1u << w) - 1)) << s;
}

bool IsRuntimeSegment(uint32_t addr) {
    const uint32_t segment = addr >> 24;
    return segment >= 0x01 && segment <= 0x0F;
}

std::string Hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", v);
    return buf;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string TypeOf(YAML::Node node) {
    return Upper(GetSafeNode<std::string>(node, "type", ""));
}

std::string EncodingOf(YAML::Node node) {
    if (TypeOf(node) == "AC:TEX_ATLAS") {
        return "gx";
    }
    return Lower(GetSafeNode<std::string>(node, "source_encoding", "n64"));
}

uint32_t Log2(uint32_t v) {
    uint32_t r = 0;
    while ((1u << r) < v) {
        r++;
    }
    return r;
}

bool IsPow2(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

uint32_t CalcDxt(uint32_t width, uint32_t bytes) {
    const uint32_t words = std::max(1u, width * bytes / 8);
    return ((1u << TX_DXT_FRAC) + words - 1) / words;
}

uint32_t CalcDxt4b(uint32_t width) {
    const uint32_t words = std::max(1u, width / 16);
    return ((1u << TX_DXT_FRAC) + words - 1) / words;
}

std::optional<std::string> N64FormatName(uint32_t fmt, uint32_t siz) {
    static const std::map<std::pair<uint32_t, uint32_t>, std::string> names = {
        { { FMT_RGBA, SIZ_16b }, "RGBA16" }, { { FMT_RGBA, SIZ_32b }, "RGBA32" }, { { FMT_CI, SIZ_4b }, "CI4" },
        { { FMT_CI, SIZ_8b }, "CI8" },       { { FMT_IA, SIZ_8b }, "IA8" },       { { FMT_IA, SIZ_16b }, "IA16" },
        { { FMT_I, SIZ_4b }, "I4" },         { { FMT_I, SIZ_8b }, "I8" },
    };
    auto it = names.find({ fmt, siz });
    if (it == names.end()) {
        return std::nullopt;
    }
    return it->second;
}

class Emitter {
public:
    explicit Emitter(std::vector<uint32_t>& out) : mOut(out) {}

    void Cmd(uint32_t w0, uint32_t w1) {
        mOut.push_back(w0);
        mOut.push_back(w1);
    }

    void Hash(const std::string& path) {
        const uint64_t hash = CRC64(path.c_str());
        if (hash == 0) {
            throw std::runtime_error("Asset hash is 0 for " + path);
        }
        Cmd(static_cast<uint32_t>(hash >> 32), static_cast<uint32_t>(hash & 0xFFFFFFFF));
    }

    void SetTile(uint32_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint32_t tile, uint32_t palette,
                 uint32_t cmt, uint32_t maskt, uint32_t shiftt, uint32_t cms, uint32_t masks, uint32_t shifts) {
        Cmd(Shift(OP_SETTILE, 24, 8) | Shift(fmt, 21, 3) | Shift(siz, 19, 2) | Shift(line, 9, 9) | Shift(tmem, 0, 9),
            Shift(tile, 24, 3) | Shift(palette, 20, 4) | Shift(cmt, 18, 2) | Shift(maskt, 14, 4) |
                Shift(shiftt, 10, 4) | Shift(cms, 8, 2) | Shift(masks, 4, 4) | Shift(shifts, 0, 4));
    }

    void LoadBlock(uint32_t tile, uint32_t lrs, uint32_t dxt) {
        if (lrs > TX_LDBLK_MAX_TXL) {
            Cmd(Shift(OP_LOADBLOCK_WIDE, 24, 8) | Shift(tile, 0, 3), lrs);
            Cmd(0, Shift(dxt, 0, 12));
        } else {
            Cmd(Shift(OP_LOADBLOCK, 24, 8), Shift(tile, 24, 3) | Shift(lrs, 12, 12) | Shift(dxt, 0, 12));
        }
    }

    void LoadTile(uint32_t tile, uint32_t lrs, uint32_t lrt) {
        Cmd(Shift(OP_LOADTILE, 24, 8), Shift(tile, 24, 3) | Shift(lrs, 12, 12) | Shift(lrt, 0, 12));
    }

    void SetTileSize(uint32_t tile, uint32_t lrs, uint32_t lrt) {
        Cmd(Shift(OP_SETTILESIZE, 24, 8), Shift(tile, 24, 3) | Shift(lrs, 12, 12) | Shift(lrt, 0, 12));
    }

    void Sync(uint8_t op) {
        Cmd(Shift(op, 24, 8), 0);
    }

private:
    std::vector<uint32_t>& mOut;
};

uint32_t RowBytes(uint32_t siz, uint32_t width) {
    return siz == SIZ_4b ? (width + 1) >> 1 : width * SIZ_INFO[siz].lineBytes;
}

bool RowIsWordAligned(uint32_t siz, uint32_t width) {
    return siz == SIZ_4b ? width % 16 == 0 : RowBytes(siz, width) % 8 == 0;
}

struct DolphinImage {
    uint32_t fmt;
    uint32_t siz;
    uint32_t width;
    uint32_t height;
    uint32_t addr;
};

void CheckVertex(uint32_t index, const std::string& name) {
    if (index >= MAX_VERTICES) {
        throw std::runtime_error(name + ": vertex index " + std::to_string(index) + " is 64 or more");
    }
}

uint32_t TriWord(const Triangle& t) {
    return Shift(t[0] * 2, 16, 8) | Shift(t[1] * 2, 8, 8) | Shift(t[2] * 2, 0, 8);
}

DListTarget RequireTarget(DListResolver& resolver, uint32_t addr, const std::string& type, const std::string& name) {
    auto target = resolver.FindAsset(addr);
    if (!target.has_value()) {
        throw std::runtime_error(name + ": no " + type + " declared at " + Hex(addr));
    }
    if (TypeOf(target->node) != type) {
        throw std::runtime_error(name + ": " + Hex(addr) + " is a " + TypeOf(target->node) + ", expected " + type);
    }
    return target.value();
}

DListTarget RequireTexture(DListResolver& resolver, uint32_t addr, bool dolphin, const std::string& name) {
    auto target = resolver.FindAsset(addr);
    if (!target.has_value()) {
        throw std::runtime_error(name + ": no TEXTURE declared at " + Hex(addr));
    }
    const auto type = TypeOf(target->node);
    if (type != "TEXTURE" && type != "AC:TEX_ATLAS") {
        throw std::runtime_error(name + ": texture load at " + Hex(addr) + " targets a " + type);
    }
    const auto encoding = EncodingOf(target->node);
    const std::string expected = dolphin ? "gx" : "n64";
    if (encoding != expected) {
        throw std::runtime_error(name + ": " + target->path + " is declared source_encoding " + encoding +
                                 " but is loaded by a " + (dolphin ? "Dolphin" : "N64") +
                                 "-form command; one symbol cannot be used with both encodings");
    }
    return target.value();
}

std::pair<uint32_t, uint32_t> WrapMode(uint32_t gx, uint32_t size, const std::string& name) {
    switch (gx) {
        case 0:
            return { TX_CLAMP, 0 };
        case 1:
        case 2:
            if (!IsPow2(size)) {
                SPDLOG_WARN("{}: {} texture dimension {} is not a power of two, clamping", name,
                            gx == 1 ? "repeating" : "mirrored", size);
                return { TX_CLAMP, 0 };
            }
            return { gx == 1 ? TX_WRAP : TX_MIRROR, Log2(size) };
        default:
            throw std::runtime_error(name + ": invalid GX wrap mode " + std::to_string(gx));
    }
}

void EmitImage(Emitter& emit, uint32_t fmt, uint32_t siz, uint32_t addr, const std::optional<DListTarget>& target,
               uint32_t width = 1) {
    const uint32_t w0 = Shift(fmt, 21, 3) | Shift(siz, 19, 2) | Shift(width - 1, 0, 12);
    if (target.has_value()) {
        emit.Cmd(Shift(G_SETTIMG_OTR_HASH, 24, 8) | w0, 0);
        emit.Hash(target->path);
    } else {
        emit.Cmd(Shift(OP_SETTIMG, 24, 8) | w0, addr | 1);
    }
}

void ExpandDolphinTile(Emitter& emit, uint32_t w0, const std::optional<DolphinImage>& image,
                       DListResolver& resolver, const std::string& name) {
    const uint32_t tile = (w0 >> 16) & 7;
    const uint32_t tlut = (w0 >> 12) & 0xF;
    const uint32_t shiftS = (w0 >> 4) & 0xF;
    const uint32_t shiftT = w0 & 0xF;

    if (!image.has_value()) {
        throw std::runtime_error(name + ": G_SETTILE_DOLPHIN without a preceding Dolphin texture image");
    }
    if (tile > 1) {
        throw std::runtime_error(name + ": Dolphin tile " + std::to_string(tile) + " is not supported");
    }

    const uint32_t fmt = image->fmt;
    uint32_t siz = image->siz;
    const uint32_t width = image->width;
    const uint32_t height = image->height;
    const uint32_t tmem = tile == 0 ? 0 : 256;

    if (!N64FormatName(fmt, siz).has_value()) {
        throw std::runtime_error(name + ": Dolphin texture format " + std::to_string(fmt) + "/" + std::to_string(siz) +
                                 " has no N64 equivalent");
    }
    if (fmt == FMT_CI && siz == SIZ_8b && tlut != 0) {
        throw std::runtime_error(name + ": CI8 texture uses TLUT slot " + std::to_string(tlut) +
                                 "; 8-bit palettes must load at slot 0");
    }

    std::optional<DListTarget> target;
    if (!IsRuntimeSegment(image->addr)) {
        target = RequireTexture(resolver, image->addr, true, name);
        const auto loaded = N64FormatName(fmt, siz).value();
        auto matches = [&](YAML::Node& n) {
            return Upper(GetSafeNode<std::string>(n, "format", "")) == loaded &&
                   GetSafeNode<uint32_t>(n, "width", 0) == width && GetSafeNode<uint32_t>(n, "height", 0) == height;
        };
        if (TypeOf(target->node) == "TEXTURE" && !matches(target->node)) {
            for (auto& variant : resolver.FindVariants(image->addr)) {
                if (TypeOf(variant.node) == "TEXTURE" && EncodingOf(variant.node) == "gx" && matches(variant.node)) {
                    target = variant;
                    break;
                }
            }
        }
        auto& node = target->node;
        if (TypeOf(node) == "TEXTURE" && !matches(node)) {
            SPDLOG_WARN("{}: {} declared {} {}x{} but loaded as {} {}x{}", name, target->path,
                        Upper(GetSafeNode<std::string>(node, "format", "")), GetSafeNode<uint32_t>(node, "width", 0),
                        GetSafeNode<uint32_t>(node, "height", 0), loaded, width, height);
        }
        if (fmt == FMT_RGBA && siz == SIZ_16b && TypeOf(node) == "TEXTURE") {
            resolver.OnDirectTexture(image->addr);
            if (!resolver.IsRgb5a3Lossless(image->addr, GetSafeNode<uint32_t>(node, "width", width),
                                           GetSafeNode<uint32_t>(node, "height", height))) {
                siz = SIZ_32b;
            }
        }
    }

    const auto [cms, masks] = WrapMode((w0 >> 10) & 3, width, name);
    const auto [cmt, maskt] = WrapMode((w0 >> 8) & 3, height, name);
    const uint32_t palette = fmt == FMT_CI && siz == SIZ_4b ? tlut : 0;
    const uint32_t texels = width * height;

    if (!RowIsWordAligned(siz, width)) {
        if (siz == SIZ_4b && (width & 1) != 0) {
            throw std::runtime_error(name + ": 4-bit texture of odd width " + std::to_string(width) +
                                     " cannot be loaded");
        }
        const uint32_t loadSiz = siz == SIZ_4b ? SIZ_8b : siz;
        const uint32_t loadWidth = siz == SIZ_4b ? width >> 1 : width;
        const uint32_t lrs = siz == SIZ_4b ? (width - 1) << 1 : (width - 1) << 2;
        const uint32_t line = (RowBytes(siz, width) + 7) >> 3;
        EmitImage(emit, fmt, loadSiz, image->addr, target, loadWidth);
        emit.SetTile(fmt, loadSiz, line, tmem, TX_LOADTILE, 0, cmt, maskt, shiftT, cms, masks, shiftS);
        emit.Sync(OP_RDPLOADSYNC);
        emit.LoadTile(TX_LOADTILE, lrs, (height - 1) << 2);
        emit.Sync(OP_RDPPIPESYNC);
        emit.SetTile(fmt, siz, line, tmem, tile, palette, cmt, maskt, shiftT, cms, masks, shiftS);
    } else if (siz == SIZ_4b) {
        EmitImage(emit, fmt, SIZ_16b, image->addr, target);
        emit.SetTile(fmt, SIZ_16b, 0, tmem, TX_LOADTILE, 0, cmt, maskt, shiftT, cms, masks, shiftS);
        emit.Sync(OP_RDPLOADSYNC);
        emit.LoadBlock(TX_LOADTILE, ((texels + 3) >> 2) - 1, CalcDxt4b(width));
        emit.Sync(OP_RDPPIPESYNC);
        emit.SetTile(fmt, SIZ_4b, ((width >> 1) + 7) >> 3, tmem, tile, palette, cmt, maskt, shiftT, cms, masks,
                     shiftS);
    } else {
        const auto& info = SIZ_INFO[siz];
        EmitImage(emit, fmt, info.loadBlock, image->addr, target);
        emit.SetTile(fmt, info.loadBlock, 0, tmem, TX_LOADTILE, 0, cmt, maskt, shiftT, cms, masks, shiftS);
        emit.Sync(OP_RDPLOADSYNC);
        emit.LoadBlock(TX_LOADTILE, ((texels + info.incr) >> info.shift) - 1, CalcDxt(width, info.bytes));
        emit.Sync(OP_RDPPIPESYNC);
        emit.SetTile(fmt, siz, ((width * info.lineBytes) + 7) >> 3, tmem, tile, palette, cmt, maskt, shiftT, cms,
                     masks, shiftS);
    }
    emit.SetTileSize(tile, (width - 1) << 2, (height - 1) << 2);
}

void ExpandDolphinTlut(Emitter& emit, uint32_t w0, uint32_t w1, DListResolver& resolver, const std::string& name) {
    if (auto mapped = resolver.MapRuntimeBuffer(w1)) {
        w1 = mapped.value();
    }
    const uint32_t slot = (w0 >> 16) & 0xF;
    uint32_t count = w0 & 0x3FFF;
    if (count == 0 || count > 1024) {
        throw std::runtime_error(name + ": Dolphin TLUT load of " + std::to_string(count) + " entries");
    }

    std::optional<DListTarget> target;
    if (!IsRuntimeSegment(w1)) {
        target = RequireTexture(resolver, w1, true, name);
        const auto format = Upper(GetSafeNode<std::string>(target->node, "format", ""));
        if (TypeOf(target->node) != "TEXTURE" || format != "TLUT") {
            throw std::runtime_error(name + ": Dolphin TLUT load targets " + target->path + ", which is not a TLUT");
        }
        const auto colors = GetSafeNode<uint32_t>(target->node, "colors", count);
        if (colors < count) {
            SPDLOG_WARN("{}: TLUT load of {} entries from {}, which declares {} colors; loading {}", name, count,
                        target->path, colors, colors);
            count = colors;
        }
    }

    EmitImage(emit, FMT_RGBA, SIZ_16b, w1, target);
    emit.Sync(OP_RDPTILESYNC);
    emit.SetTile(0, 0, 0, 256 + 16 * slot, TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    emit.Sync(OP_RDPLOADSYNC);
    emit.Cmd(Shift(OP_LOADTLUT, 24, 8), Shift(TX_LOADTILE, 24, 3) | Shift(count - 1, 14, 10));
    emit.Sync(OP_RDPPIPESYNC);
}

} // namespace

size_t DecodeTrin(const uint32_t* words, size_t gfxCount, std::vector<Triangle>& out) {
    int remaining = (int)((words[0] >> 17) & 0x7F) + 1;
    size_t used = 0;

    for (bool first = true; remaining > 0; first = false, used++) {
        if (used >= gfxCount) {
            throw std::runtime_error("G_TRIN run extends past the end of the display list");
        }
        const uint32_t w0 = words[used * 2];
        const uint32_t w1 = words[used * 2 + 1];
        uint32_t tris[4][3];
        int count;

        if ((w1 & 1) == 0) {
            const uint32_t packed[4] = {
                (w1 >> 4) & 0x7FFF,
                ((w1 >> 19) & 0x1FFF) | ((w0 & 3) << 13),
                (w0 >> 2) & 0x7FFF,
                (w0 >> 17) & 0x7FFF,
            };
            for (int i = 0; i < 4; i++) {
                tris[i][0] = packed[i] & 0x1F;
                tris[i][1] = (packed[i] >> 5) & 0x1F;
                tris[i][2] = (packed[i] >> 10) & 0x1F;
            }
            count = first ? 3 : 4;
        } else {
            const uint32_t packed[3][3] = {
                { (w1 >> 1) & 0x7F, (w1 >> 8) & 0x7F, (w1 >> 15) & 0x7F },
                { (w1 >> 22) & 0x7F, ((w1 >> 29) & 7) | ((w0 & 0xF) << 3), (w0 >> 4) & 0x7F },
                { (w0 >> 11) & 0x7F, (w0 >> 18) & 0x7F, (w0 >> 25) & 0x7F },
            };
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    tris[i][j] = packed[i][j];
                }
            }
            count = first ? 2 : 3;
        }

        for (int i = 0; i < count && remaining > 0; i++, remaining--) {
            out.push_back({ (uint8_t)tris[i][0], (uint8_t)tris[i][1], (uint8_t)tris[i][2] });
        }
    }

    return used;
}

size_t CommandLength(const uint32_t* words, size_t gfxCount) {
    const uint8_t op = words[0] >> 24;
    if (op == OP_TRIN || op == OP_TRIN_INDEPEND) {
        std::vector<Triangle> tris;
        return DecodeTrin(words, gfxCount, tris);
    }
    return 1;
}

std::vector<uint32_t> NormalizeDisplayList(const std::vector<uint32_t>& words, DListResolver& resolver,
                                           const std::string& name, std::vector<uint32_t>* indexMap) {
    std::vector<uint32_t> out;
    Emitter emit(out);
    const size_t count = words.size() / 2;
    std::optional<DolphinImage> image;
    bool imageUsed = true;
    if (indexMap != nullptr) {
        indexMap->assign(count, NO_INDEX);
    }

    for (size_t i = 0; i < count;) {
        uint32_t w0 = words[i * 2];
        uint32_t w1 = words[i * 2 + 1];
        const uint8_t op = w0 >> 24;
        if (indexMap != nullptr) {
            (*indexMap)[i] = static_cast<uint32_t>(out.size() / 2);
        }

        switch (op) {
            case OP_TRIN:
            case OP_TRIN_INDEPEND: {
                std::vector<Triangle> tris;
                const size_t used = DecodeTrin(&words[i * 2], count - i, tris);
                for (const auto& t : tris) {
                    CheckVertex(t[0], name);
                    CheckVertex(t[1], name);
                    CheckVertex(t[2], name);
                }
                for (size_t k = 0; k < tris.size(); k += 2) {
                    if (k + 1 < tris.size()) {
                        emit.Cmd(Shift(OP_TRI2, 24, 8) | TriWord(tris[k]), TriWord(tris[k + 1]));
                    } else {
                        emit.Cmd(Shift(OP_TRI1, 24, 8) | TriWord(tris[k]), 0);
                    }
                }
                i += used;
                continue;
            }
            case OP_QUADN:
                throw std::runtime_error(name + ": G_QUADN is not supported in static display lists");
            case OP_TRI1:
            case OP_TRI2:
            case OP_QUAD:
                for (int shift = 0; shift <= 16; shift += 8) {
                    CheckVertex(((w0 >> shift) & 0xFF) / 2, name);
                    if (op != OP_TRI1) {
                        CheckVertex(((w1 >> shift) & 0xFF) / 2, name);
                    }
                }
                emit.Cmd(w0, w1);
                break;
            case OP_VTX: {
                const uint32_t end = (w0 >> 1) & 0x7F;
                if (end > MAX_VERTICES) {
                    throw std::runtime_error(name + ": vertex index " + std::to_string(end - 1) + " is 64 or more");
                }
                if (auto mapped = resolver.MapRuntimeBuffer(w1)) {
                    emit.Cmd(w0, mapped.value());
                    break;
                }
                if (IsRuntimeSegment(w1)) {
                    emit.Cmd(w0, w1 | 1);
                    break;
                }
                auto target = resolver.FindVtx(w1);
                if (!target.has_value()) {
                    throw std::runtime_error(name + ": no VTX declared at " + Hex(w1));
                }
                emit.Cmd(Shift(G_VTX_OTR_HASH, 24, 8) | (w0 & 0x00FFFFFF), target->offset);
                emit.Hash(target->path);
                break;
            }
            case OP_DL: {
                if (IsRuntimeSegment(w1) && (w1 & 0x00FFFFFF) != 0) {
                    const uint32_t segment = w1 >> 24;
                    const auto index = resolver.DlIndex(segment, w1 & 0x00FFFFFF, name);
                    if (!index.has_value()) {
                        throw std::runtime_error(name + ": gsSPDisplayList(" + Hex(w1) +
                                                 ") calls into the middle of a segment and needs a GFX "
                                                 "hints: dl_index entry for segment " +
                                                 std::to_string(segment));
                    }
                    emit.Cmd(Shift(OP_DL_INDEX, 24, 8) | (w0 & 0x00FF0000), (segment << 24) | index.value());
                    break;
                }
                if (IsRuntimeSegment(w1)) {
                    emit.Cmd(w0, w1 | 1);
                    break;
                }
                const auto target = RequireTarget(resolver, w1, "GFX", name);
                emit.Cmd(Shift(G_DL_OTR_HASH, 24, 8), 0);
                emit.Hash(target.path);
                if ((w0 >> 16) & G_DL_NO_PUSH) {
                    emit.Cmd(Shift(OP_ENDDL, 24, 8), 0);
                }
                break;
            }
            case OP_MTX: {
                if (IsRuntimeSegment(w1)) {
                    emit.Cmd(w0, w1 | 1);
                    break;
                }
                const auto target = RequireTarget(resolver, w1, "MTX", name);
                emit.Cmd(Shift(G_MTX_OTR, 24, 8) | (w0 & 0x00FFFFFF), 0);
                emit.Hash(target.path);
                break;
            }
            case OP_MOVEMEM: {
                if (IsRuntimeSegment(w1)) {
                    emit.Cmd(w0, w1 | 1);
                    break;
                }
                const uint32_t index = w0 & 0xFF;
                const uint32_t offset = ((w0 >> 8) & 0xFF) * 8;
                auto target = resolver.FindAsset(w1);
                bool interior = false;
                if (!target.has_value()) {
                    target = resolver.FindAsset(w1 - 8);
                    interior = target.has_value();
                }
                if (!target.has_value()) {
                    throw std::runtime_error(name + ": no asset declared for G_MOVEMEM at " + Hex(w1));
                }
                emit.Cmd(Shift(G_MOVEMEM_OTR_HASH, 24, 8) | (w0 & 0x00FFFFFF),
                         Shift(index, 24, 8) | Shift(offset, 16, 8) | Shift(interior ? 1 : 0, 8, 8));
                emit.Hash(target->path);
                break;
            }
            case OP_SETTIMG: {
                const auto mapped = resolver.MapRuntimeBuffer(w1);
                if (w0 & DOLPHIN_IMAGE_BIT) {
                    if (!imageUsed) {
                        SPDLOG_WARN("{}: Dolphin texture image {} is never bound to a tile", name, Hex(image->addr));
                    }
                    image = DolphinImage{ (w0 >> 21) & 7, (w0 >> 19) & 3, (w0 & 0x3FF) + 1,
                                          (((w0 >> 10) & 0xFF) + 1) * 4, mapped.value_or(w1) };
                    imageUsed = false;
                    break;
                }
                image.reset();
                imageUsed = true;
                if (mapped.has_value()) {
                    emit.Cmd(w0, mapped.value());
                    break;
                }
                if (IsRuntimeSegment(w1)) {
                    emit.Cmd(w0, w1 | 1);
                    break;
                }
                const auto target = RequireTexture(resolver, w1, false, name);
                emit.Cmd(Shift(G_SETTIMG_OTR_HASH, 24, 8) | (w0 & 0x00FFFFFF), 0);
                emit.Hash(target.path);
                break;
            }
            case OP_SETTILE_DOLPHIN:
                ExpandDolphinTile(emit, w0, image, resolver, name);
                imageUsed = true;
                break;
            case OP_LOADTLUT:
                if (((w0 >> 22) & 3) == TLUT_DOLPHIN) {
                    ExpandDolphinTlut(emit, w0, w1, resolver, name);
                } else {
                    emit.Cmd(w0, w1);
                }
                break;
            default:
                emit.Cmd(w0, w1);
                break;
        }
        i++;
    }

    if (!imageUsed) {
        SPDLOG_WARN("{}: Dolphin texture image {} is never bound to a tile", name, Hex(image->addr));
    }
    return out;
}

void WriteDisplayListV1(std::ostream& write, const std::vector<uint32_t>& words, const std::string& path) {
    auto writer = LUS::BinaryWriter();
    BaseExporter::WriteHeader(writer, Torch::ResourceType::DisplayList, 1);

    writer.Write((int8_t)GBIVersion::f3dex2);
    while (writer.GetBaseAddress() % 8 != 0) {
        writer.Write(static_cast<int8_t>(0xFF));
    }

    writer.Write(static_cast<uint32_t>(words.size() / 2 + 2));
    writer.Write(static_cast<uint32_t>(0));

    const uint64_t hash = CRC64(path.c_str());
    writer.Write(static_cast<uint32_t>(G_MARKER << 24));
    writer.Write(static_cast<uint32_t>(0xBEEFBEEF));
    writer.Write(static_cast<uint32_t>(hash >> 32));
    writer.Write(static_cast<uint32_t>(hash & 0xFFFFFFFF));

    for (const auto word : words) {
        writer.Write(word);
    }
    writer.Finish(write);
}

namespace DListHelpers {

namespace {

std::mutex sMutex;
std::set<uint32_t> sDirectTextures;
std::map<uint32_t, bool> sLosslessCache;

std::optional<DListTarget> FindInCurrentFile(uint32_t addr) {
    const auto* nodes = Companion::Instance->GetNodesByTypeRef("VTX", true);
    if (nodes == nullptr) {
        return std::nullopt;
    }
    for (const auto& [path, node] : *nodes) {
        auto& n = const_cast<YAML::Node&>(node);
        const auto offset = GetSafeNode<uint32_t>(n, "offset");
        const auto count = GetSafeNode<uint32_t>(n, "count");
        if (addr >= offset && addr < offset + count * 16) {
            return DListTarget{ path, n, addr - offset };
        }
    }
    return std::nullopt;
}

std::map<uint32_t, std::vector<uint32_t>> sIndexMaps;

std::vector<uint32_t> ReadDisplayListWords(YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto size = GetSafeNode<uint32_t>(node, "size");
    auto [_, segment] = Decompressor::AutoDecode(offset, size, Companion::Instance->GetRomData());
    if (segment.size < size) {
        throw std::runtime_error("Display list at " + Hex(offset) + " is truncated");
    }
    LUS::BinaryReader reader(segment.data, size);
    reader.SetEndianness(Torch::Endianness::Big);
    std::vector<uint32_t> words;
    for (uint32_t i = 0; i < size / 4; i++) {
        words.push_back(reader.ReadUInt32());
    }
    return words;
}

class CompanionResolver : public DListResolver {
public:
    explicit CompanionResolver(YAML::Node node = YAML::Node()) : mNode(node) {}

    std::optional<DListTarget> FindAsset(uint32_t addr) override {
        auto found = Companion::Instance->GetNodeByAddr(addr);
        if (!found.has_value()) {
            return std::nullopt;
        }
        return DListTarget{ std::get<0>(found.value()), std::get<1>(found.value()), 0 };
    }

    std::optional<DListTarget> FindVtx(uint32_t addr) override {
        auto exact = FindAsset(addr);
        if (exact.has_value() && TypeOf(exact->node) == "VTX") {
            return exact;
        }
        if (auto local = FindInCurrentFile(addr)) {
            return local;
        }
        if (Companion::Instance->IsGlobalAddrSpace()) {
            if (auto global = Companion::Instance->SearchGlobalVtx(addr)) {
                auto node = std::get<1>(global.value());
                return DListTarget{ std::get<0>(global.value()), node, addr - GetSafeNode<uint32_t>(node, "offset") };
            }
        }
        return std::nullopt;
    }

    bool IsRgb5a3Lossless(uint32_t addr, uint32_t width, uint32_t height) override {
#ifdef GX_TEXTURE_SUPPORT
        {
            std::lock_guard<std::mutex> lock(sMutex);
            if (auto it = sLosslessCache.find(addr); it != sLosslessCache.end()) {
                return it->second;
            }
        }
        const auto size = GX::TiledSize(GX::TexFormat::RGB5A3, width, height);
        auto [_, segment] = Decompressor::AutoDecode(addr, size, Companion::Instance->GetRomData());
        if (segment.size < size) {
            throw std::runtime_error("RGB5A3 texture at " + Hex(addr) + " is truncated");
        }
        const bool lossless = GX::Rgb5a3TextureIsLossless(segment.data, segment.size, width, height);
        std::lock_guard<std::mutex> lock(sMutex);
        sLosslessCache[addr] = lossless;
        return lossless;
#else
        return true;
#endif
    }

    void OnDirectTexture(uint32_t addr) override {
        std::lock_guard<std::mutex> lock(sMutex);
        sDirectTextures.insert(addr);
    }

    std::optional<uint32_t> MapRuntimeBuffer(uint32_t addr) override {
        return Companion::Instance->MapRuntimeBuffer(addr);
    }

    std::vector<DListTarget> FindVariants(uint32_t addr) override {
        std::vector<DListTarget> out;
        for (auto& [path, node] : Companion::Instance->GetVariantsByAddr(addr)) {
            out.push_back(DListTarget{ path, node, 0 });
        }
        return out;
    }

    std::optional<uint32_t> DlIndex(uint32_t segment, uint32_t offset, const std::string& name) override {
        if (!mNode.IsDefined() || !mNode.IsMap() || !mNode["hints"] || !mNode["hints"]["dl_index"]) {
            return std::nullopt;
        }
        for (const auto& entry : mNode["hints"]["dl_index"]) {
            if (entry["segment"].as<uint32_t>() != segment) {
                continue;
            }
            std::optional<uint32_t> agreed;
            std::string first;
            for (const auto& t : entry["targets"]) {
                const auto addr = t.as<uint32_t>();
                auto found = FindAsset(addr);
                if (!found.has_value() || TypeOf(found->node) != "GFX") {
                    throw std::runtime_error(name + ": dl_index target " + Hex(addr) + " is not a declared GFX");
                }
                const auto map = IndexMap(addr, found.value());
                const uint32_t command = offset / 8;
                if (offset % 8 != 0 || command >= map.size() || map[command] == NO_INDEX) {
                    throw std::runtime_error(name + ": offset 0x" + Torch::to_hex(offset, false) +
                                             " is not a command boundary of " + found->path);
                }
                const uint32_t index = DL_V1_PREFIX + map[command];
                if (agreed.has_value() && agreed.value() != index) {
                    throw std::runtime_error(name + ": dl_index targets disagree for segment " +
                                             std::to_string(segment) + " offset 0x" + Torch::to_hex(offset, false) +
                                             ": " + first + " gives " + std::to_string(agreed.value()) + ", " +
                                             found->path + " gives " + std::to_string(index));
                }
                if (!agreed.has_value()) {
                    first = found->path;
                }
                agreed = index;
            }
            return agreed;
        }
        return std::nullopt;
    }

private:
    static std::vector<uint32_t> IndexMap(uint32_t addr, DListTarget& target) {
        {
            std::lock_guard<std::mutex> lock(sMutex);
            if (auto it = sIndexMaps.find(addr); it != sIndexMaps.end()) {
                return it->second;
            }
        }
        const auto words = ReadDisplayListWords(target.node);
        CompanionResolver resolver(target.node);
        std::vector<uint32_t> map;
        NormalizeDisplayList(words, resolver, target.path, &map);
        std::lock_guard<std::mutex> lock(sMutex);
        sIndexMaps[addr] = map;
        return map;
    }

    YAML::Node mNode;
};

void AddDependency(YAML::Node asset) {
    Companion::Instance->AddAsset(asset);
}

} // namespace

std::optional<ExportResult> Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                   YAML::Node& node, std::string* replacement) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::AC) {
        return std::nullopt;
    }

    const auto& words = std::static_pointer_cast<DListData>(raw)->mGfxs;
    CompanionResolver resolver(node);
    const auto normalized = NormalizeDisplayList(words, resolver, *replacement);
    WriteDisplayListV1(write, normalized, *replacement);
    return ExportResult(std::nullopt);
}

std::optional<std::optional<std::shared_ptr<IParsedData>>> Parse(std::vector<uint8_t>& raw_buffer, YAML::Node& node) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::AC) {
        return std::nullopt;
    }

    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    std::vector<uint32_t> words;

    auto readWords = [](const DataChunk& segment, size_t gfxCount, std::vector<uint32_t>& out) {
        LUS::BinaryReader reader(segment.data, segment.size);
        reader.SetEndianness(Torch::Endianness::Big);
        for (size_t i = 0; i < gfxCount * 2; i++) {
            out.push_back(reader.ReadUInt32());
        }
    };

    if (node["size"]) {
        const auto size = GetSafeNode<uint32_t>(node, "size");
        if (size % 8 != 0) {
            throw std::runtime_error("Display list size 0x" + Torch::to_hex(size, false) + " is not a multiple of 8");
        }
        auto [_, segment] = Decompressor::AutoDecode(node, raw_buffer, size);
        if (segment.size < size) {
            throw std::runtime_error("Display list at " + Hex(offset) + " is truncated");
        }
        readWords(segment, size / 8, words);
    } else {
        auto [_, segment] = Decompressor::AutoDecode(node, raw_buffer);
        std::vector<uint32_t> all;
        readWords(segment, segment.size / 8, all);
        const size_t total = all.size() / 2;
        size_t i = 0;
        while (i < total) {
            const uint32_t w0 = all[i * 2];
            const uint8_t op = w0 >> 24;
            i += CommandLength(&all[i * 2], total - i);
            if (op == OP_ENDDL || (op == OP_DL && ((w0 >> 16) & G_DL_NO_PUSH))) {
                break;
            }
        }
        if (i == total && total != 0 && (all[(i - 1) * 2] >> 24) != OP_ENDDL) {
            SPDLOG_WARN("Display list at 0x{:X} has no G_ENDDL and no declared size", offset);
        }
        words.assign(all.begin(), all.begin() + i * 2);
    }

    CompanionResolver resolver;
    const size_t total = words.size() / 2;
    for (size_t i = 0; i < total;) {
        const uint32_t w0 = words[i * 2];
        const uint32_t w1 = words[i * 2 + 1];
        const uint8_t op = w0 >> 24;
        const size_t length = CommandLength(&words[i * 2], total - i);
        i += length;

        if (length > 1 || IsRuntimeSegment(w1) || resolver.MapRuntimeBuffer(w1).has_value()) {
            continue;
        }

        switch (op) {
            case OP_DL: {
                YAML::Node gfx;
                gfx["type"] = "GFX";
                gfx["offset"] = w1;
                AddDependency(gfx);
                break;
            }
            case OP_VTX:
                if (!resolver.FindVtx(w1).has_value()) {
                    YAML::Node vtx;
                    vtx["type"] = "VTX";
                    vtx["offset"] = w1;
                    vtx["count"] = (w0 >> 12) & 0xFF;
                    AddDependency(vtx);
                }
                break;
            case OP_MOVEMEM:
                if ((w0 & 0xFF) == MV_LIGHT && ((w0 >> 8) & 0xFF) * 8 == (2 * 24 + 24)) {
                    YAML::Node lights;
                    lights["type"] = "LIGHTS";
                    lights["offset"] = w1;
                    AddDependency(lights);
                }
                break;
            case OP_SETTIMG:
                if ((w0 & DOLPHIN_IMAGE_BIT) && !resolver.FindAsset(w1).has_value()) {
                    const auto format = N64FormatName((w0 >> 21) & 7, (w0 >> 19) & 3);
                    if (!format.has_value()) {
                        throw std::runtime_error("Dolphin texture at " + Hex(w1) + " has no N64 format");
                    }
                    YAML::Node texture;
                    texture["type"] = "TEXTURE";
                    texture["offset"] = w1;
                    texture["format"] = format.value();
                    texture["width"] = (w0 & 0x3FF) + 1;
                    texture["height"] = (((w0 >> 10) & 0xFF) + 1) * 4;
                    texture["source_encoding"] = "gx";
                    AddDependency(texture);
                }
                break;
            case OP_LOADTLUT:
                if (((w0 >> 22) & 3) == TLUT_DOLPHIN && !resolver.FindAsset(w1).has_value()) {
                    YAML::Node tlut;
                    tlut["type"] = "TEXTURE";
                    tlut["offset"] = w1;
                    tlut["format"] = "TLUT";
                    tlut["colors"] = w0 & 0x3FFF;
                    tlut["source_encoding"] = "gx";
                    AddDependency(tlut);
                }
                break;
            default:
                break;
        }
    }

    return std::make_optional(
        std::make_optional(std::static_pointer_cast<IParsedData>(std::make_shared<DListData>(words))));
}

void Reset() {
    std::lock_guard<std::mutex> lock(sMutex);
    sDirectTextures.clear();
    sLosslessCache.clear();
    sIndexMaps.clear();
#ifdef GX_TEXTURE_SUPPORT
    GX::GetStats().Clear();
#endif
}

void Finalize() {
#ifdef GX_TEXTURE_SUPPORT
    auto& stats = GX::GetStats();
    if (stats.GetPartialAlphaTluts() != 0) {
        SPDLOG_INFO("{} TLUT entries in {} TLUTs have partial alpha (A3 in 1..6); those TLUTs are 32-bit",
                    stats.GetPartialAlphaEntries(), stats.GetPartialAlphaTluts());
    } else {
        SPDLOG_INFO("No GX TLUT entry has partial alpha");
    }

    std::string unreached;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        for (const auto& [addr, symbol] : stats.GetPromotions()) {
            if (!sDirectTextures.contains(addr)) {
                unreached += "\n  " + symbol + " (" + Hex(addr) + ")";
            }
        }
    }
    if (!unreached.empty()) {
        throw std::runtime_error("RGBA32 promotion of textures that no static display list loads directly "
                                 "(reached through a segment or code):" +
                                 unreached);
    }
#endif
}

} // namespace DListHelpers
} // namespace AC
