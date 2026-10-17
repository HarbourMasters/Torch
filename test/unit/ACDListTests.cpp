#include <gtest/gtest.h>

#include "factories/ac/ACDListHelpers.h"
#include "gc/GXTexture.h"
#include <strhash64/StrHash64.h>

#include <cstring>
#include <map>
#include <set>
#include <sstream>

using namespace AC;

namespace {

#define REF_SHIFTL(v, s, w) ((uint32_t)(((uint32_t)(v) & ((0x01u << (w)) - 1)) << (s)))
#define REF_SHIFTR(v, s, w) ((uint32_t)(((uint32_t)(v) >> (s)) & ((0x01u << (w)) - 1)))

constexpr uint32_t G_TRIN = 0x09;
constexpr uint32_t G_TRIN_INDEPEND = 0x0A;
constexpr uint32_t G_SETTIMG = 0xFD;
constexpr uint32_t G_SETTILE = 0xF5;
constexpr uint32_t G_SETTILE_DOLPHIN = 0xD2;
constexpr uint32_t G_LOADBLOCK = 0xF3;
constexpr uint32_t G_LOADTILE = 0xF4;
constexpr uint32_t G_SETTILESIZE = 0xF2;
constexpr uint32_t G_LOADTLUT = 0xF0;
constexpr uint32_t G_RDPLOADSYNC = 0xE6;
constexpr uint32_t G_RDPPIPESYNC = 0xE7;
constexpr uint32_t G_RDPTILESYNC = 0xE8;
constexpr uint32_t G_ENDDL = 0xDF;
constexpr uint32_t G_DL = 0xDE;
constexpr uint32_t G_MTX = 0xDA;
constexpr uint32_t G_VTX = 0x01;
constexpr uint32_t G_TRI1 = 0x05;
constexpr uint32_t G_TRI2 = 0x06;
constexpr uint32_t OTR_SETTIMG = 0x20;
constexpr uint32_t OTR_DL = 0x31;
constexpr uint32_t OTR_VTX = 0x32;
constexpr uint32_t G_TX_LOADTILE = 7;
constexpr uint32_t G_TX_WRAP = 0;
constexpr uint32_t G_TX_MIRROR = 1;
constexpr uint32_t G_TX_CLAMP = 2;
constexpr uint32_t G_IM_FMT_RGBA = 0;
constexpr uint32_t G_IM_FMT_CI = 2;
constexpr uint32_t G_IM_SIZ_4b = 0;
constexpr uint32_t G_IM_SIZ_8b = 1;
constexpr uint32_t G_IM_SIZ_16b = 2;
constexpr uint32_t G_IM_SIZ_32b = 3;
constexpr uint32_t GX_CLAMP = 0;
constexpr uint32_t GX_REPEAT = 1;
constexpr uint32_t GX_MIRROR = 2;

using Words = std::vector<uint32_t>;

uint32_t TriData5(uint32_t v0, uint32_t v1, uint32_t v2) {
    return REF_SHIFTL(v2, 10, 5) | REF_SHIFTL(v1, 5, 5) | REF_SHIFTL(v0, 0, 5);
}

uint64_t TriData7(uint64_t v0, uint64_t v1, uint64_t v2) {
    return (v2 << 14) | (v1 << 7) | v0;
}

void TrinInit5b(Words& w, uint32_t op, uint32_t n, Triangle a, Triangle b, Triangle c) {
    w.push_back(REF_SHIFTL(op, 24, 8) | REF_SHIFTL(n - 1, 17, 7) | REF_SHIFTL(TriData5(c[0], c[1], c[2]), 2, 15) |
                REF_SHIFTL(REF_SHIFTR(TriData5(b[0], b[1], b[2]), 13, 2), 0, 2));
    w.push_back(REF_SHIFTL(TriData5(b[0], b[1], b[2]), 19, 13) | REF_SHIFTL(TriData5(a[0], a[1], a[2]), 4, 15));
}

void Trin5b(Words& w, Triangle a, Triangle b, Triangle c, Triangle d) {
    w.push_back(REF_SHIFTL(TriData5(d[0], d[1], d[2]), 17, 15) | REF_SHIFTL(TriData5(c[0], c[1], c[2]), 2, 15) |
                REF_SHIFTL(REF_SHIFTR(TriData5(b[0], b[1], b[2]), 13, 2), 0, 2));
    w.push_back(REF_SHIFTL(TriData5(b[0], b[1], b[2]), 19, 13) | REF_SHIFTL(TriData5(a[0], a[1], a[2]), 4, 15));
}

void TrinInit7b(Words& w, uint32_t op, uint32_t n, Triangle a, Triangle b) {
    const uint64_t head = (uint64_t)(REF_SHIFTL(op, 24, 8) | REF_SHIFTL(n - 1, 17, 7)) << 32;
    const uint64_t g = head | (TriData7(b[0], b[1], b[2]) << 22) | (TriData7(a[0], a[1], a[2]) << 1) | 1;
    w.push_back((uint32_t)(g >> 32));
    w.push_back((uint32_t)g);
}

void Trin7b(Words& w, Triangle a, Triangle b, Triangle c) {
    const uint64_t g =
        (TriData7(c[0], c[1], c[2]) << 43) | (TriData7(b[0], b[1], b[2]) << 22) | (TriData7(a[0], a[1], a[2]) << 1) | 1;
    w.push_back((uint32_t)(g >> 32));
    w.push_back((uint32_t)g);
}

void Cmd(Words& w, uint32_t w0, uint32_t w1) {
    w.push_back(w0);
    w.push_back(w1);
}

uint32_t Tri1W(Triangle t) {
    return REF_SHIFTL(t[0] * 2, 16, 8) | REF_SHIFTL(t[1] * 2, 8, 8) | REF_SHIFTL(t[2] * 2, 0, 8);
}

void Tri2(Words& w, Triangle a, Triangle b) {
    Cmd(w, REF_SHIFTL(G_TRI2, 24, 8) | Tri1W(a), Tri1W(b));
}

void Tri1(Words& w, Triangle a) {
    Cmd(w, REF_SHIFTL(G_TRI1, 24, 8) | Tri1W(a), 0);
}

void SetTimgDolphin(Words& w, uint32_t fmt, uint32_t siz, uint32_t width, uint32_t height, uint32_t img) {
    Cmd(w,
        REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(fmt, 21, 3) | REF_SHIFTL(siz, 19, 2) | REF_SHIFTL(1, 18, 1) |
            REF_SHIFTL((height / 4) - 1, 10, 8) | REF_SHIFTL((width - 1), 0, 10),
        img);
}

void SetTileDolphin(Words& w, uint32_t tile, uint32_t tlut, uint32_t ws, uint32_t wt, uint32_t ss, uint32_t st) {
    Cmd(w,
        REF_SHIFTL(G_SETTILE_DOLPHIN, 24, 8) | REF_SHIFTL(15, 20, 4) | REF_SHIFTL(tile, 16, 3) | REF_SHIFTL(tlut, 12, 4) |
            REF_SHIFTL(ws, 10, 2) | REF_SHIFTL(wt, 8, 2) | REF_SHIFTL(ss, 4, 4) | REF_SHIFTL(st, 0, 4),
        0);
}

void LoadTlutDolphin(Words& w, uint32_t name, uint32_t count, uint32_t addr) {
    Cmd(w, REF_SHIFTL(G_LOADTLUT, 24, 8) | REF_SHIFTL(2, 22, 2) | REF_SHIFTL(name, 16, 4) | REF_SHIFTL(count, 0, 14),
        addr);
}

void Hash(Words& w, const std::string& path) {
    const uint64_t h = CRC64(path.c_str());
    Cmd(w, (uint32_t)(h >> 32), (uint32_t)h);
}

void OtrImage(Words& w, uint32_t fmt, uint32_t siz, const std::string& path) {
    Cmd(w, REF_SHIFTL(OTR_SETTIMG, 24, 8) | REF_SHIFTL(fmt, 21, 3) | REF_SHIFTL(siz, 19, 2), 0);
    Hash(w, path);
}

void OtrImageWidth(Words& w, uint32_t fmt, uint32_t siz, uint32_t width, const std::string& path) {
    Cmd(w, REF_SHIFTL(OTR_SETTIMG, 24, 8) | REF_SHIFTL(fmt, 21, 3) | REF_SHIFTL(siz, 19, 2) | REF_SHIFTL(width - 1, 0, 12),
        0);
    Hash(w, path);
}

void SetTile(Words& w, uint32_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint32_t tile, uint32_t palette,
             uint32_t cmt, uint32_t maskt, uint32_t shiftt, uint32_t cms, uint32_t masks, uint32_t shifts) {
    Cmd(w,
        (REF_SHIFTL(G_SETTILE, 24, 8) | REF_SHIFTL(fmt, 21, 3) | REF_SHIFTL(siz, 19, 2) | REF_SHIFTL(line, 9, 9) |
         REF_SHIFTL(tmem, 0, 9)),
        (REF_SHIFTL(tile, 24, 3) | REF_SHIFTL(palette, 20, 4) | REF_SHIFTL(cmt, 18, 2) | REF_SHIFTL(maskt, 14, 4) |
         REF_SHIFTL(shiftt, 10, 4) | REF_SHIFTL(cms, 8, 2) | REF_SHIFTL(masks, 4, 4) | REF_SHIFTL(shifts, 0, 4)));
}

void LoadBlock(Words& w, uint32_t tile, uint32_t lrs, uint32_t dxt) {
    Cmd(w, REF_SHIFTL(G_LOADBLOCK, 24, 8),
        REF_SHIFTL(tile, 24, 3) | REF_SHIFTL(std::min(lrs, 4095u), 12, 12) | REF_SHIFTL(dxt, 0, 12));
}

void LoadTile(Words& w, uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    Cmd(w, REF_SHIFTL(G_LOADTILE, 24, 8) | REF_SHIFTL(uls, 12, 12) | REF_SHIFTL(ult, 0, 12),
        REF_SHIFTL(tile, 24, 3) | REF_SHIFTL(lrs, 12, 12) | REF_SHIFTL(lrt, 0, 12));
}

void SetTileSize(Words& w, uint32_t tile, uint32_t lrs, uint32_t lrt) {
    Cmd(w, REF_SHIFTL(G_SETTILESIZE, 24, 8), REF_SHIFTL(tile, 24, 3) | REF_SHIFTL(lrs, 12, 12) | REF_SHIFTL(lrt, 0, 12));
}

void Sync(Words& w, uint32_t op) {
    Cmd(w, REF_SHIFTL(op, 24, 8), 0);
}

uint32_t Txl2Words(uint32_t txls, uint32_t b) {
    return std::max(1u, txls * b / 8);
}

uint32_t CalcDxt(uint32_t width, uint32_t b) {
    return ((1u << 11) + Txl2Words(width, b) - 1) / Txl2Words(width, b);
}

uint32_t CalcDxt4b(uint32_t width) {
    const uint32_t words = std::max(1u, width / 16);
    return ((1u << 11) + words - 1) / words;
}

void LoadMultiBlock4bTail(Words& w, uint32_t tmem, uint32_t rtile, uint32_t fmt, uint32_t width, uint32_t height,
                          uint32_t pal, uint32_t cms, uint32_t cmt, uint32_t masks, uint32_t maskt, uint32_t shifts,
                          uint32_t shiftt) {
    SetTile(w, fmt, G_IM_SIZ_16b, 0, tmem, G_TX_LOADTILE, 0, cmt, maskt, shiftt, cms, masks, shifts);
    Sync(w, G_RDPLOADSYNC);
    LoadBlock(w, G_TX_LOADTILE, (((width) * (height) + 3) >> 2) - 1, CalcDxt4b(width));
    Sync(w, G_RDPPIPESYNC);
    SetTile(w, fmt, G_IM_SIZ_4b, ((((width) >> 1) + 7) >> 3), tmem, rtile, pal, cmt, maskt, shiftt, cms, masks, shifts);
    SetTileSize(w, rtile, 0 + ((width - 1) << 2), ((height - 1) << 2));
}

void LoadMultiBlockTail(Words& w, uint32_t tmem, uint32_t rtile, uint32_t fmt, uint32_t siz, uint32_t loadSiz,
                        uint32_t incr, uint32_t shift, uint32_t bytes, uint32_t lineBytes, uint32_t width,
                        uint32_t height, uint32_t pal, uint32_t cms, uint32_t cmt, uint32_t masks, uint32_t maskt,
                        uint32_t shifts, uint32_t shiftt) {
    SetTile(w, fmt, loadSiz, 0, tmem, G_TX_LOADTILE, 0, cmt, maskt, shiftt, cms, masks, shifts);
    Sync(w, G_RDPLOADSYNC);
    LoadBlock(w, G_TX_LOADTILE, (((width) * (height) + incr) >> shift) - 1, CalcDxt(width, bytes));
    Sync(w, G_RDPPIPESYNC);
    SetTile(w, fmt, siz, ((((width)*lineBytes) + 7) >> 3), tmem, rtile, pal, cmt, maskt, shiftt, cms, masks, shifts);
    SetTileSize(w, rtile, ((width - 1) << 2), ((height - 1) << 2));
}

void LoadMultiTile4bTail(Words& w, uint32_t tmem, uint32_t rtile, uint32_t fmt, uint32_t width, uint32_t height,
                         uint32_t pal, uint32_t cms, uint32_t cmt, uint32_t masks, uint32_t maskt, uint32_t shifts,
                         uint32_t shiftt) {
    const uint32_t lrs = width - 1, lrt = height - 1;
    SetTile(w, fmt, G_IM_SIZ_8b, ((((lrs) - (0) + 1) >> 1) + 7) >> 3, tmem, G_TX_LOADTILE, 0, cmt, maskt, shiftt, cms,
            masks, shifts);
    Sync(w, G_RDPLOADSYNC);
    LoadTile(w, G_TX_LOADTILE, (0) << (2 - 1), (0) << 2, (lrs) << (2 - 1), (lrt) << 2);
    Sync(w, G_RDPPIPESYNC);
    SetTile(w, fmt, G_IM_SIZ_4b, ((((lrs) - (0) + 1) >> 1) + 7) >> 3, tmem, rtile, pal, cmt, maskt, shiftt, cms, masks,
            shifts);
    SetTileSize(w, rtile, (lrs) << 2, (lrt) << 2);
}

void LoadMultiTileTail(Words& w, uint32_t tmem, uint32_t rtile, uint32_t fmt, uint32_t siz, uint32_t tileBytes,
                       uint32_t width, uint32_t height, uint32_t pal, uint32_t cms, uint32_t cmt, uint32_t masks,
                       uint32_t maskt, uint32_t shifts, uint32_t shiftt) {
    const uint32_t lrs = width - 1, lrt = height - 1;
    SetTile(w, fmt, siz, ((((lrs) - (0) + 1) * tileBytes) + 7) >> 3, tmem, G_TX_LOADTILE, 0, cmt, maskt, shiftt, cms,
            masks, shifts);
    Sync(w, G_RDPLOADSYNC);
    LoadTile(w, G_TX_LOADTILE, (0) << 2, (0) << 2, (lrs) << 2, (lrt) << 2);
    Sync(w, G_RDPPIPESYNC);
    SetTile(w, fmt, siz, ((((lrs) - (0) + 1) * tileBytes) + 7) >> 3, tmem, rtile, pal, cmt, maskt, shiftt, cms, masks,
            shifts);
    SetTileSize(w, rtile, (lrs) << 2, (lrt) << 2);
}

class FakeResolver : public DListResolver {
public:
    void Add(uint32_t addr, const std::string& path, const std::string& yaml) {
        auto node = YAML::Load(yaml);
        node["offset"] = addr;
        mAssets[addr] = { path, node, 0 };
    }

    std::optional<DListTarget> FindAsset(uint32_t addr) override {
        auto it = mAssets.find(addr);
        if (it == mAssets.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<DListTarget> FindVtx(uint32_t addr) override {
        for (auto& [start, target] : mAssets) {
            if (target.node["type"].as<std::string>() != "VTX") {
                continue;
            }
            const uint32_t end = start + target.node["count"].as<uint32_t>() * 16;
            if (addr >= start && addr < end) {
                auto out = target;
                out.offset = addr - start;
                return out;
            }
        }
        return std::nullopt;
    }

    bool IsRgb5a3Lossless(uint32_t addr, uint32_t, uint32_t) override {
        return !mLossy.contains(addr);
    }

    void OnDirectTexture(uint32_t addr) override {
        mDirect.insert(addr);
    }

    std::map<uint32_t, DListTarget> mAssets;
    std::set<uint32_t> mLossy;
    std::set<uint32_t> mDirect;
};

void ExpectWords(const Words& actual, const Words& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); i++) {
        EXPECT_EQ(actual[i], expected[i]) << "word " << i << " (gfx " << i / 2 << ")";
    }
}

} // namespace

TEST(ACDList, MixedTrinRunBecomesTri2InOrder) {
    const Triangle t[10] = { { 0, 1, 2 },    { 3, 4, 5 },    { 6, 7, 8 },    { 40, 41, 42 }, { 43, 44, 45 },
                             { 46, 47, 63 }, { 9, 10, 11 },  { 12, 13, 14 }, { 15, 16, 17 }, { 5, 28, 27 } };
    Words in;
    TrinInit5b(in, G_TRIN_INDEPEND, 10, t[0], t[1], t[2]);
    Trin7b(in, t[3], t[4], t[5]);
    Trin5b(in, t[6], t[7], t[8], t[9]);
    Cmd(in, REF_SHIFTL(G_ENDDL, 24, 8), 0);
    EXPECT_EQ(in[3 * 2] >> 24, 0xDFu) << "the last continuation word must look like G_ENDDL";

    std::vector<Triangle> decoded;
    EXPECT_EQ(DecodeTrin(in.data(), in.size() / 2, decoded), 3u);
    ASSERT_EQ(decoded.size(), 10u);
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(decoded[i], t[i]) << "triangle " << i;
    }

    FakeResolver resolver;
    Words expected;
    for (int i = 0; i < 10; i += 2) {
        Tri2(expected, t[i], t[i + 1]);
    }
    Cmd(expected, REF_SHIFTL(G_ENDDL, 24, 8), 0);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
}

TEST(ACDList, TrinRunsEndWithTri1AndStopAtTheCount) {
    const Triangle a = { 1, 2, 3 }, b = { 4, 5, 6 }, c = { 7, 8, 9 }, d = { 10, 11, 12 }, e = { 13, 14, 15 };
    Words in;
    TrinInit7b(in, G_TRIN, 5, a, b);
    Trin7b(in, c, d, e);
    TrinInit5b(in, G_TRIN_INDEPEND, 4, a, b, c);
    Trin5b(in, d, { 31, 31, 31 }, { 31, 31, 31 }, { 31, 31, 31 });
    TrinInit5b(in, G_TRIN, 1, e, { 0, 0, 0 }, { 0, 0, 0 });

    FakeResolver resolver;
    Words expected;
    Tri2(expected, a, b);
    Tri2(expected, c, d);
    Tri1(expected, e);
    Tri2(expected, a, b);
    Tri2(expected, c, d);
    Tri1(expected, e);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
}

TEST(ACDList, TrinRunPastTheEndIsAnError) {
    Words in;
    TrinInit5b(in, G_TRIN_INDEPEND, 9, { 0, 1, 2 }, { 0, 1, 2 }, { 0, 1, 2 });
    FakeResolver resolver;
    EXPECT_THROW(NormalizeDisplayList(in, resolver, "dl"), std::runtime_error);
}

TEST(ACDList, DolphinTileZeroBecomesLoadTextureBlock4b) {
    FakeResolver resolver;
    resolver.Add(0x81000100, "data/tu/tex",
                 "{type: TEXTURE, format: CI4, width: 32, height: 16, source_encoding: gx}");

    Words in;
    SetTimgDolphin(in, G_IM_FMT_CI, G_IM_SIZ_4b, 32, 16, 0x81000100);
    SetTileDolphin(in, 0, 2, GX_MIRROR, GX_REPEAT, 1, 2);

    Words expected;
    OtrImage(expected, G_IM_FMT_CI, G_IM_SIZ_16b, "data/tu/tex");
    LoadMultiBlock4bTail(expected, 0, 0, G_IM_FMT_CI, 32, 16, 2, G_TX_MIRROR, G_TX_WRAP, 5, 4, 1, 2);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
}

TEST(ACDList, DolphinTileOneLoadsAtTmem256) {
    FakeResolver resolver;
    resolver.Add(0x81000800, "data/tu/tex8", "{type: TEXTURE, format: CI8, width: 16, height: 16, source_encoding: gx}");
    resolver.Add(0x81000900, "data/tu/ia", "{type: TEXTURE, format: IA16, width: 8, height: 8, source_encoding: gx}");

    Words in;
    SetTimgDolphin(in, G_IM_FMT_CI, G_IM_SIZ_8b, 16, 16, 0x81000800);
    SetTileDolphin(in, 1, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    SetTimgDolphin(in, 3, G_IM_SIZ_16b, 8, 8, 0x81000900);
    SetTileDolphin(in, 0, 0, GX_REPEAT, GX_MIRROR, 0, 0);

    Words expected;
    OtrImage(expected, G_IM_FMT_CI, G_IM_SIZ_16b, "data/tu/tex8");
    LoadMultiBlockTail(expected, 256, 1, G_IM_FMT_CI, G_IM_SIZ_8b, G_IM_SIZ_16b, 1, 1, 1, 1, 16, 16, 0, G_TX_CLAMP,
                       G_TX_CLAMP, 0, 0, 0, 0);
    OtrImage(expected, 3, G_IM_SIZ_16b, "data/tu/ia");
    LoadMultiBlockTail(expected, 0, 0, 3, G_IM_SIZ_16b, G_IM_SIZ_16b, 0, 0, 2, 2, 8, 8, 0, G_TX_WRAP, G_TX_MIRROR, 3,
                       3, 0, 0);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
}

TEST(ACDList, RowsThatAreNotWholeWordsUseLoadTile) {
    FakeResolver resolver;
    resolver.Add(0x80100000, "static/bootdata/logo_nin/nintendo_376x104",
                 "{type: TEXTURE, format: I4, width: 376, height: 104, source_encoding: gx}");
    resolver.Add(0x81000100, "data/tu/ci4_8x16", "{type: TEXTURE, format: CI4, width: 8, height: 16, source_encoding: gx}");
    resolver.Add(0x81000200, "data/tu/ci4_24x24",
                 "{type: TEXTURE, format: CI4, width: 24, height: 24, source_encoding: gx}");

    Words in;
    SetTimgDolphin(in, 4, G_IM_SIZ_4b, 376, 104, 0x80100000);
    SetTileDolphin(in, 0, 15, GX_CLAMP, GX_CLAMP, 0, 0);
    SetTimgDolphin(in, G_IM_FMT_CI, G_IM_SIZ_4b, 8, 16, 0x81000100);
    SetTileDolphin(in, 1, 3, GX_REPEAT, GX_MIRROR, 0, 0);
    SetTimgDolphin(in, G_IM_FMT_CI, G_IM_SIZ_4b, 24, 24, 0x81000200);
    SetTileDolphin(in, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);

    Words expected;
    OtrImageWidth(expected, 4, G_IM_SIZ_8b, 376 >> 1, "static/bootdata/logo_nin/nintendo_376x104");
    LoadMultiTile4bTail(expected, 0, 0, 4, 376, 104, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
    OtrImageWidth(expected, G_IM_FMT_CI, G_IM_SIZ_8b, 8 >> 1, "data/tu/ci4_8x16");
    LoadMultiTile4bTail(expected, 256, 1, G_IM_FMT_CI, 8, 16, 3, G_TX_WRAP, G_TX_MIRROR, 3, 4, 0, 0);
    OtrImageWidth(expected, G_IM_FMT_CI, G_IM_SIZ_8b, 24 >> 1, "data/tu/ci4_24x24");
    LoadMultiTile4bTail(expected, 0, 0, G_IM_FMT_CI, 24, 24, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);

    EXPECT_EQ((expected[6 * 2] >> 9) & 0x1FF, 24u);
    EXPECT_EQ(((expected[4 * 2 + 1] >> 12) & 0xFFF) >> 1, 375u);
}

TEST(ACDList, UnalignedRowsAtOtherDepthsUseLoadTile) {
    FakeResolver resolver;
    resolver.Add(0x81000300, "data/tu/i8_12x8", "{type: TEXTURE, format: I8, width: 12, height: 8, source_encoding: gx}");
    resolver.Add(0x81000400, "data/tu/rgba_6x4", "{type: TEXTURE, format: RGBA16, width: 6, height: 4, source_encoding: gx}");
    resolver.Add(0x81000500, "data/tu/lossy_6x4",
                 "{type: TEXTURE, format: RGBA16, width: 6, height: 4, source_encoding: gx}");
    resolver.mLossy.insert(0x81000500);

    Words in;
    SetTimgDolphin(in, 4, G_IM_SIZ_8b, 12, 8, 0x81000300);
    SetTileDolphin(in, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    SetTimgDolphin(in, G_IM_FMT_RGBA, G_IM_SIZ_16b, 6, 4, 0x81000400);
    SetTileDolphin(in, 1, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    SetTimgDolphin(in, G_IM_FMT_RGBA, G_IM_SIZ_16b, 6, 4, 0x81000500);
    SetTileDolphin(in, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);

    Words expected;
    OtrImageWidth(expected, 4, G_IM_SIZ_8b, 12, "data/tu/i8_12x8");
    LoadMultiTileTail(expected, 0, 0, 4, G_IM_SIZ_8b, 1, 12, 8, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
    OtrImageWidth(expected, G_IM_FMT_RGBA, G_IM_SIZ_16b, 6, "data/tu/rgba_6x4");
    LoadMultiTileTail(expected, 256, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, 2, 6, 4, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
    OtrImageWidth(expected, G_IM_FMT_RGBA, G_IM_SIZ_32b, 6, "data/tu/lossy_6x4");
    LoadMultiTileTail(expected, 0, 0, G_IM_FMT_RGBA, G_IM_SIZ_32b, 2, 6, 4, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
}

TEST(ACDList, OddWidth4bTextureIsAnError) {
    FakeResolver resolver;
    resolver.Add(0x81000100, "data/tu/odd", "{type: TEXTURE, format: I4, width: 7, height: 8, source_encoding: gx}");
    Words in;
    SetTimgDolphin(in, 4, G_IM_SIZ_4b, 7, 8, 0x81000100);
    SetTileDolphin(in, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    EXPECT_THROW(NormalizeDisplayList(in, resolver, "dl"), std::runtime_error);
}

TEST(ACDList, DolphinTlutLoadsAtSlotTmem) {
    FakeResolver resolver;
    resolver.Add(0x81000200, "data/tu/pal", "{type: TEXTURE, format: TLUT, colors: 16, source_encoding: gx}");

    Words in;
    LoadTlutDolphin(in, 3, 16, 0x81000200);
    LoadTlutDolphin(in, 0, 16, 0x0A000020);

    Words expected;
    OtrImage(expected, G_IM_FMT_RGBA, G_IM_SIZ_16b, "data/tu/pal");
    Sync(expected, G_RDPTILESYNC);
    SetTile(expected, 0, 0, 0, 256 + 16 * 3, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    Sync(expected, G_RDPLOADSYNC);
    Cmd(expected, REF_SHIFTL(G_LOADTLUT, 24, 8), REF_SHIFTL(G_TX_LOADTILE, 24, 3) | REF_SHIFTL(15, 14, 10));
    Sync(expected, G_RDPPIPESYNC);
    Cmd(expected, REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(G_IM_FMT_RGBA, 21, 3) | REF_SHIFTL(G_IM_SIZ_16b, 19, 2),
        0x0A000021);
    Sync(expected, G_RDPTILESYNC);
    SetTile(expected, 0, 0, 0, 256, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    Sync(expected, G_RDPLOADSYNC);
    Cmd(expected, REF_SHIFTL(G_LOADTLUT, 24, 8), REF_SHIFTL(G_TX_LOADTILE, 24, 3) | REF_SHIFTL(15, 14, 10));
    Sync(expected, G_RDPPIPESYNC);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
}

TEST(ACDList, TlutLoadIsClampedToTheDeclaredColors) {
    FakeResolver resolver;
    resolver.Add(0x81000200, "data/tu/pal16", "{type: TEXTURE, format: TLUT, colors: 16, source_encoding: gx}");

    Words in;
    LoadTlutDolphin(in, 0, 256, 0x81000200);

    Words expected;
    OtrImage(expected, G_IM_FMT_RGBA, G_IM_SIZ_16b, "data/tu/pal16");
    Sync(expected, G_RDPTILESYNC);
    SetTile(expected, 0, 0, 0, 256, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    Sync(expected, G_RDPLOADSYNC);
    Cmd(expected, REF_SHIFTL(G_LOADTLUT, 24, 8), REF_SHIFTL(G_TX_LOADTILE, 24, 3) | REF_SHIFTL(15, 14, 10));
    Sync(expected, G_RDPPIPESYNC);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
}

TEST(ACDList, RuntimeSegmentsAreTaggedNotResolved) {
    FakeResolver resolver;
    Words in;
    Cmd(in, REF_SHIFTL(G_VTX, 24, 8) | REF_SHIFTL(4, 12, 8) | REF_SHIFTL(4, 1, 7), 0x08000040);
    Cmd(in, REF_SHIFTL(G_DL, 24, 8), 0x0B000000);
    Cmd(in, REF_SHIFTL(G_MTX, 24, 8) | 0x380000, 0x0D000040);
    Cmd(in, REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(G_IM_FMT_CI, 21, 3) | REF_SHIFTL(G_IM_SIZ_16b, 19, 2), 0x0C000000);
    SetTimgDolphin(in, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 8, 0x09000040);
    SetTileDolphin(in, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);

    Words expected;
    Cmd(expected, in[0], 0x08000041);
    Cmd(expected, in[2], 0x0B000001);
    Cmd(expected, in[4], 0x0D000041);
    Cmd(expected, in[6], 0x0C000001);
    Cmd(expected, REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(G_IM_FMT_RGBA, 21, 3) | REF_SHIFTL(G_IM_SIZ_16b, 19, 2),
        0x09000041);
    LoadMultiBlockTail(expected, 0, 0, G_IM_FMT_RGBA, G_IM_SIZ_16b, G_IM_SIZ_16b, 0, 0, 2, 2, 16, 8, 0, G_TX_CLAMP,
                       G_TX_CLAMP, 0, 0, 0, 0);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
    EXPECT_TRUE(resolver.mDirect.empty());
}

TEST(ACDList, StaticPointersBecomeOtrHashes) {
    FakeResolver resolver;
    resolver.Add(0x81000400, "data/a/vtx", "{type: VTX, count: 8}");
    resolver.Add(0x81002000, "data/b/sub_dl", "{type: GFX, size: 8}");
    resolver.Add(0x80100000, "data/c/n64tex", "{type: TEXTURE, format: RGBA16, width: 4, height: 4}");

    Words in;
    const uint32_t vtxW0 = REF_SHIFTL(G_VTX, 24, 8) | REF_SHIFTL(4, 12, 8) | REF_SHIFTL(4, 1, 7);
    Cmd(in, vtxW0, 0x81000420);
    Cmd(in, REF_SHIFTL(G_DL, 24, 8), 0x81002000);
    const uint32_t timgW0 =
        REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(G_IM_FMT_RGBA, 21, 3) | REF_SHIFTL(G_IM_SIZ_16b, 19, 2);
    Cmd(in, timgW0, 0x80100000);
    Cmd(in, REF_SHIFTL(G_DL, 24, 8) | REF_SHIFTL(1, 16, 8), 0x81002000);

    Words expected;
    Cmd(expected, REF_SHIFTL(OTR_VTX, 24, 8) | (vtxW0 & 0xFFFFFF), 0x20);
    Hash(expected, "data/a/vtx");
    Cmd(expected, REF_SHIFTL(OTR_DL, 24, 8), 0);
    Hash(expected, "data/b/sub_dl");
    Cmd(expected, REF_SHIFTL(OTR_SETTIMG, 24, 8) | (timgW0 & 0xFFFFFF), 0);
    Hash(expected, "data/c/n64tex");
    Cmd(expected, REF_SHIFTL(OTR_DL, 24, 8), 0);
    Hash(expected, "data/b/sub_dl");
    Cmd(expected, REF_SHIFTL(G_ENDDL, 24, 8), 0);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
}

TEST(ACDList, LossyRgba16IsLoadedAs32Bit) {
    FakeResolver resolver;
    resolver.Add(0x81000300, "data/tu/rgba", "{type: TEXTURE, format: RGBA16, width: 16, height: 8, source_encoding: gx}");
    resolver.mLossy.insert(0x81000300);

    Words in;
    SetTimgDolphin(in, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 8, 0x81000300);
    SetTileDolphin(in, 0, 0, GX_REPEAT, GX_REPEAT, 0, 0);

    Words expected;
    OtrImage(expected, G_IM_FMT_RGBA, G_IM_SIZ_32b, "data/tu/rgba");
    LoadMultiBlockTail(expected, 0, 0, G_IM_FMT_RGBA, G_IM_SIZ_32b, G_IM_SIZ_32b, 0, 0, 4, 2, 16, 8, 0, G_TX_WRAP,
                       G_TX_WRAP, 4, 3, 0, 0);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
    EXPECT_TRUE(resolver.mDirect.contains(0x81000300));
}

TEST(ACDList, MixedEncodingIsAnError) {
    FakeResolver resolver;
    resolver.Add(0x81000100, "data/tu/n64", "{type: TEXTURE, format: CI4, width: 16, height: 16}");
    resolver.Add(0x81000200, "data/tu/gx", "{type: TEXTURE, format: CI4, width: 16, height: 16, source_encoding: gx}");

    Words dolphinOfN64;
    SetTimgDolphin(dolphinOfN64, G_IM_FMT_CI, G_IM_SIZ_4b, 16, 16, 0x81000100);
    SetTileDolphin(dolphinOfN64, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    EXPECT_THROW(NormalizeDisplayList(dolphinOfN64, resolver, "dl"), std::runtime_error);

    Words n64OfGx;
    Cmd(n64OfGx, REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(G_IM_FMT_CI, 21, 3) | REF_SHIFTL(G_IM_SIZ_16b, 19, 2),
        0x81000200);
    EXPECT_THROW(NormalizeDisplayList(n64OfGx, resolver, "dl"), std::runtime_error);
}

TEST(ACDList, VertexIndexOver63IsAnError) {
    FakeResolver resolver;
    Words trin;
    TrinInit7b(trin, G_TRIN_INDEPEND, 2, { 0, 1, 2 }, { 62, 63, 64 });
    EXPECT_THROW(NormalizeDisplayList(trin, resolver, "dl"), std::runtime_error);

    Words vtx;
    Cmd(vtx, REF_SHIFTL(G_VTX, 24, 8) | REF_SHIFTL(32, 12, 8) | REF_SHIFTL(40 + 32, 1, 7), 0x08000000);
    EXPECT_THROW(NormalizeDisplayList(vtx, resolver, "dl"), std::runtime_error);

    Words ok;
    Cmd(ok, REF_SHIFTL(G_VTX, 24, 8) | REF_SHIFTL(32, 12, 8) | REF_SHIFTL(32 + 32, 1, 7), 0x08000000);
    EXPECT_NO_THROW(NormalizeDisplayList(ok, resolver, "dl"));
}

TEST(ACDList, UnsupportedFormsAreErrors) {
    FakeResolver resolver;
    resolver.Add(0x81000800, "data/tu/tex8", "{type: TEXTURE, format: CI8, width: 16, height: 16, source_encoding: gx}");

    Words quadn;
    Cmd(quadn, REF_SHIFTL(0x0D, 24, 8), 0);
    EXPECT_THROW(NormalizeDisplayList(quadn, resolver, "dl"), std::runtime_error);

    Words ci8Slot;
    SetTimgDolphin(ci8Slot, G_IM_FMT_CI, G_IM_SIZ_8b, 16, 16, 0x81000800);
    SetTileDolphin(ci8Slot, 0, 2, GX_CLAMP, GX_CLAMP, 0, 0);
    EXPECT_THROW(NormalizeDisplayList(ci8Slot, resolver, "dl"), std::runtime_error);

    Words noImage;
    SetTileDolphin(noImage, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    EXPECT_THROW(NormalizeDisplayList(noImage, resolver, "dl"), std::runtime_error);

    Words undeclared;
    Cmd(undeclared, REF_SHIFTL(G_DL, 24, 8), 0x81009000);
    EXPECT_THROW(NormalizeDisplayList(undeclared, resolver, "dl"), std::runtime_error);
}

TEST(ACDList, PromotionWithoutADirectLoadFailsFinalize) {
    DListHelpers::Reset();
    GX::GetStats().RecordPromotion(0x81000100, "data/tu/atlas_tex");
    EXPECT_THROW(DListHelpers::Finalize(), std::runtime_error);
    DListHelpers::Reset();
    EXPECT_NO_THROW(DListHelpers::Finalize());
}

TEST(ACDList, WritesDisplayListV1) {
    const Words words = { 0xE7000000, 0, 0xDF000000, 0 };
    std::ostringstream out;
    WriteDisplayListV1(out, words, "data/tu/dl");
    const auto s = out.str();
    ASSERT_EQ(s.size(), 0x40u + 8 + 8 + 16 + 16);

    uint32_t type, version, count;
    std::memcpy(&type, s.data() + 4, 4);
    std::memcpy(&version, s.data() + 8, 4);
    EXPECT_EQ(type, 0x4F444C54u);
    EXPECT_EQ(version, 1u);
    EXPECT_EQ((uint8_t)s[0x40], 4);
    for (int i = 0x41; i < 0x48; i++) {
        EXPECT_EQ((uint8_t)s[i], 0xFF);
    }
    std::memcpy(&count, s.data() + 0x48, 4);
    EXPECT_EQ(count, 4u);

    uint32_t marker, beef, hi, lo, first;
    std::memcpy(&marker, s.data() + 0x50, 4);
    std::memcpy(&beef, s.data() + 0x54, 4);
    std::memcpy(&hi, s.data() + 0x58, 4);
    std::memcpy(&lo, s.data() + 0x5C, 4);
    std::memcpy(&first, s.data() + 0x60, 4);
    const uint64_t hash = CRC64("data/tu/dl");
    EXPECT_EQ(marker, 0x33000000u);
    EXPECT_EQ(beef, 0xBEEFBEEFu);
    EXPECT_EQ(hi, (uint32_t)(hash >> 32));
    EXPECT_EQ(lo, (uint32_t)hash);
    EXPECT_EQ(first, 0xE7000000u);
}

namespace {

class BufferResolver : public FakeResolver {
public:
    std::optional<uint32_t> MapRuntimeBuffer(uint32_t addr) override {
        if (addr >= 0x82100400 && addr < 0x82100C00) {
            return ((0x0Fu << 24) | (0x100 + (addr - 0x82100400))) | 1;
        }
        return std::nullopt;
    }

    std::vector<DListTarget> FindVariants(uint32_t addr) override {
        std::vector<DListTarget> out;
        for (auto& [a, t] : mVariants) {
            if (a == addr) {
                out.push_back(t);
            }
        }
        return out;
    }

    std::optional<uint32_t> DlIndex(uint32_t segment, uint32_t offset, const std::string&) override {
        if (segment != 0x0B) {
            return std::nullopt;
        }
        return DL_V1_PREFIX + offset / 8;
    }

    void AddVariant(uint32_t addr, const std::string& path, const std::string& yaml) {
        auto node = YAML::Load(yaml);
        node["offset"] = addr;
        mVariants.push_back({ addr, DListTarget{ path, node, 0 } });
    }

    std::vector<std::pair<uint32_t, DListTarget>> mVariants;
};

} // namespace

TEST(ACDList, RuntimeBufferOperandsUseSegmentF) {
    BufferResolver resolver;
    Words in;
    LoadTlutDolphin(in, 15, 16, 0x82100400);
    SetTimgDolphin(in, G_IM_FMT_CI, G_IM_SIZ_4b, 32, 32, 0x82100420);
    SetTileDolphin(in, 0, 15, GX_REPEAT, GX_REPEAT, 0, 0);
    Cmd(in, REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(G_IM_FMT_CI, 21, 3) | REF_SHIFTL(G_IM_SIZ_16b, 19, 2), 0x82100420);
    Cmd(in, REF_SHIFTL(G_VTX, 24, 8) | REF_SHIFTL(4, 12, 8) | REF_SHIFTL(4, 1, 7), 0x82100800);

    Words expected;
    Cmd(expected, REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(G_IM_FMT_RGBA, 21, 3) | REF_SHIFTL(G_IM_SIZ_16b, 19, 2),
        0x0F000101);
    Sync(expected, G_RDPTILESYNC);
    SetTile(expected, 0, 0, 0, 256 + 16 * 15, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    Sync(expected, G_RDPLOADSYNC);
    Cmd(expected, REF_SHIFTL(G_LOADTLUT, 24, 8), REF_SHIFTL(G_TX_LOADTILE, 24, 3) | REF_SHIFTL(15, 14, 10));
    Sync(expected, G_RDPPIPESYNC);
    Cmd(expected, REF_SHIFTL(G_SETTIMG, 24, 8) | REF_SHIFTL(G_IM_FMT_CI, 21, 3) | REF_SHIFTL(G_IM_SIZ_16b, 19, 2),
        0x0F000121);
    LoadMultiBlock4bTail(expected, 0, 0, G_IM_FMT_CI, 32, 32, 15, G_TX_WRAP, G_TX_WRAP, 5, 5, 0, 0);
    Cmd(expected, in[in.size() - 4], 0x0F000121);
    Cmd(expected, in[in.size() - 2], 0x0F000501);
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);
    EXPECT_TRUE(resolver.mDirect.empty());
}

TEST(ACDList, VariantMatchingTheLoadShapeIsUsed) {
    BufferResolver resolver;
    resolver.Add(0x81000100, "data/tu/tex", "{type: TEXTURE, format: IA8, width: 64, height: 32, source_encoding: gx}");
    resolver.AddVariant(0x81000100, "data/tu/tex@IA8_32x64",
                        "{type: TEXTURE, format: IA8, width: 32, height: 64, source_encoding: gx}");

    Words in;
    SetTimgDolphin(in, 3, G_IM_SIZ_8b, 32, 64, 0x81000100);
    SetTileDolphin(in, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    SetTimgDolphin(in, 3, G_IM_SIZ_8b, 64, 32, 0x81000100);
    SetTileDolphin(in, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);

    const auto out = NormalizeDisplayList(in, resolver, "dl");
    const uint64_t variant = CRC64("data/tu/tex@IA8_32x64");
    const uint64_t primary = CRC64("data/tu/tex");
    std::vector<uint64_t> hashes;
    for (size_t i = 0; i + 3 < out.size(); i += 2) {
        if ((out[i] >> 24) == OTR_SETTIMG) {
            hashes.push_back(((uint64_t)out[i + 2] << 32) | out[i + 3]);
        }
    }
    ASSERT_EQ(hashes.size(), 2u);
    EXPECT_EQ(hashes[0], variant);
    EXPECT_EQ(hashes[1], primary);
}

TEST(ACDList, SegmentOffsetDisplayListBecomesDlIndex) {
    BufferResolver resolver;
    Words in;
    Cmd(in, REF_SHIFTL(G_DL, 24, 8), 0x0B000058);
    Cmd(in, REF_SHIFTL(G_DL, 24, 8) | REF_SHIFTL(1, 16, 8), 0x0B000018);

    Words expected;
    Cmd(expected, REF_SHIFTL(0x3D, 24, 8), 0x0B000000 | (2 + 0x58 / 8));
    Cmd(expected, REF_SHIFTL(0x3D, 24, 8) | REF_SHIFTL(1, 16, 8), 0x0B000000 | (2 + 0x18 / 8));
    ExpectWords(NormalizeDisplayList(in, resolver, "dl"), expected);

    Words unhinted;
    Cmd(unhinted, REF_SHIFTL(G_DL, 24, 8), 0x08000030);
    EXPECT_THROW(NormalizeDisplayList(unhinted, resolver, "dl"), std::runtime_error);
}

TEST(ACDList, IndexMapFollowsExpansions) {
    FakeResolver resolver;
    resolver.Add(0x81000100, "data/tu/tex", "{type: TEXTURE, format: CI4, width: 32, height: 16, source_encoding: gx}");
    Words in;
    Cmd(in, 0xE7000000, 0);
    SetTimgDolphin(in, G_IM_FMT_CI, G_IM_SIZ_4b, 32, 16, 0x81000100);
    SetTileDolphin(in, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    TrinInit5b(in, G_TRIN, 5, { 0, 1, 2 }, { 1, 2, 3 }, { 2, 3, 4 });
    Trin5b(in, { 3, 4, 5 }, { 4, 5, 6 }, { 0, 0, 0 }, { 0, 0, 0 });
    Cmd(in, 0xDF000000, 0);

    std::vector<uint32_t> map;
    const auto out = NormalizeDisplayList(in, resolver, "dl", &map);
    ASSERT_EQ(map.size(), 6u);
    EXPECT_EQ(map[0], 0u);
    EXPECT_EQ(map[1], 1u) << "a Dolphin image emits nothing until its tile";
    EXPECT_EQ(map[2], 1u);
    EXPECT_EQ(map[3], 1u + 2 + 6) << "OTR image (2) + LoadTextureBlock_4b tail (6)";
    EXPECT_EQ(map[4], NO_INDEX) << "TRIN continuation words have no index";
    EXPECT_EQ(map[5], map[3] + 3) << "five triangles become two G_TRI2 and one G_TRI1";
    EXPECT_EQ(out[map[5] * 2] >> 24, 0xDFu);
}
