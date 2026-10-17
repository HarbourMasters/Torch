#include <gtest/gtest.h>

#include "Companion.h"
#include "SyntheticDisc.h"
#include "TestUtils.h"
#include "factories/ac/KeyFrameAnimFactory.h"
#include "gc/GXTexture.h"
#include "input/gc/GameCubeInput.h"
#include <strhash64/StrHash64.h>

#include <cstring>

namespace fs = std::filesystem;
using namespace TestUtils;
using Synthetic::Rel;

namespace {

constexpr uint32_t DL_JOINT = 0x000;
constexpr uint32_t JOINTS = 0x010;
constexpr uint32_t SKEL = 0x028;
constexpr uint32_t FLAGS = 0x030;
constexpr uint32_t KEYS = 0x034;
constexpr uint32_t KF_DATA = 0x040;
constexpr uint32_t FIXED = 0x068;
constexpr uint32_t ANIM = 0x070;
constexpr uint32_t NPC = 0x084;
constexpr uint32_t EYE = 0x0C4;
constexpr uint32_t FEEL = 0x0CC;
constexpr uint32_t FEEL_SET = 0x0D4;
constexpr uint32_t SE_TABLE = 0x0DC;
constexpr uint32_t SE_DATA = 0x0EC;
constexpr uint32_t SE_FRAMES = 0x0F4;
constexpr uint32_t SE_OTHER = 0x0FC;
constexpr uint32_t SCROLL = 0x108;
constexpr uint32_t COLREG = 0x110;
constexpr uint32_t PRIM = 0x120;
constexpr uint32_t ENV = 0x12C;
constexpr uint32_t COL_KEYS = 0x134;
constexpr uint32_t TEXANIME = 0x138;
constexpr uint32_t TEX_TABLE = 0x148;
constexpr uint32_t PATTERN = 0x150;
constexpr uint32_t EVW = 0x158;
constexpr uint32_t TEX_A = 0x180;
constexpr uint32_t TEX_B = 0x1A0;
constexpr uint32_t ATLAS = 0x1C0;
constexpr uint32_t PAL_ALPHA = 0x200;
constexpr uint32_t DL_TARGET = 0x220;
constexpr uint32_t DL_MAIN = 0x248;
constexpr uint32_t DATA_SIZE = 0x280;
constexpr uint32_t BSS_TEX = 0x00;
constexpr uint32_t BSS_PAL = 0x80;

constexpr uint8_t DATA_SECTION = 5;
constexpr uint8_t BSS_SECTION = 6;

struct Reloc {
    uint32_t at;
    uint8_t section;
    uint32_t target;
};

std::vector<uint8_t> TileC4(const std::vector<uint8_t>& linear, uint32_t w, uint32_t h) {
    std::vector<uint8_t> out(GX::TiledSize(GX::TexFormat::C4, w, h), 0);
    const uint32_t tilesX = (w + 7) / 8;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const size_t index = ((y / 8) * tilesX + x / 8) * 64 + (y % 8) * 8 + (x % 8);
            const size_t src = static_cast<size_t>(y) * w + x;
            const uint8_t v = (src & 1) ? linear[src / 2] & 0xF : linear[src / 2] >> 4;
            out[index / 2] |= (index & 1) ? v : v << 4;
        }
    }
    return out;
}

uint32_t Native32(const std::vector<uint8_t>& buf, size_t offset) {
    uint32_t v;
    std::memcpy(&v, buf.data() + offset, 4);
    return v;
}

uint16_t Native16(const std::vector<uint8_t>& buf, size_t offset) {
    uint16_t v;
    std::memcpy(&v, buf.data() + offset, 2);
    return v;
}

uint64_t Native64(const std::vector<uint8_t>& buf, size_t offset) {
    uint64_t v;
    std::memcpy(&v, buf.data() + offset, 8);
    return v;
}

void Put16s(std::vector<uint8_t>& data, uint32_t offset, const std::vector<int16_t>& values) {
    for (size_t i = 0; i < values.size(); i++) {
        PutBE16(data, offset + i * 2, (uint16_t)values[i]);
    }
}

void PutWords(std::vector<uint8_t>& data, uint32_t offset, const std::vector<uint32_t>& words) {
    for (size_t i = 0; i < words.size(); i++) {
        PutBE32(data, offset + i * 4, words[i]);
    }
}

void PutFloat(std::vector<uint8_t>& data, uint32_t offset, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    PutBE32(data, offset, v);
}

struct Disc {
    fs::path root;
    fs::path disc;
    fs::path src;
    fs::path out;
    std::vector<uint8_t> dol;
    std::string relSha1;
    std::vector<uint8_t> texA;
    std::vector<uint8_t> texB;
    std::vector<uint8_t> atlas0;
    std::vector<uint8_t> atlas1;
    std::vector<uint8_t> palAlpha;
    std::vector<uint8_t> fg;
    std::vector<uint8_t> item0;
};

Disc MakeDisc(const std::string& name) {
    Disc d;
    d.root = MakeTempDir(name);
    d.disc = d.root / "disc";
    d.src = d.root / "src";
    d.out = d.root / "out";
    d.dol = Synthetic::BuildDol({ { 0x80003100, Pattern(0x40, 30) } }, { { 0x80005000, Pattern(0x20, 31) } },
                                0x80005020, 0x40);

    std::vector<uint8_t> data(DATA_SIZE, 0);
    std::vector<Reloc> relocs;
    auto ptr = [&](uint32_t at, uint32_t target, uint8_t section = DATA_SECTION) {
        relocs.push_back({ at, section, target });
    };

    PutWords(data, DL_JOINT, { 0xDF000000, 0 });
    ptr(JOINTS, DL_JOINT);
    data[JOINTS + 4] = 1;
    data[JOINTS + 5] = 0;
    Put16s(data, JOINTS + 6, { 1, 2, 3 });
    data[JOINTS + 12 + 4] = 0;
    data[JOINTS + 12 + 5] = 1;
    Put16s(data, JOINTS + 12 + 6, { -4, 5, -6 });
    data[SKEL] = 2;
    data[SKEL + 1] = 1;
    ptr(SKEL + 4, JOINTS);

    data[FLAGS] = 0x3C;
    data[FLAGS + 1] = 0x01;
    Put16s(data, KEYS, { 1, 2, 1, 1, 1 });
    for (int i = 0; i < 18; i++) {
        PutBE16(data, KF_DATA + i * 2, (uint16_t)(i * 3 - 7));
    }
    Put16s(data, FIXED, { 10, -20, 30, -40 });
    auto anim = [&](uint32_t at) {
        ptr(at, FLAGS);
        ptr(at + 4, KF_DATA);
        ptr(at + 8, KEYS);
        ptr(at + 12, FIXED);
        Put16s(data, at + 0x10, { -1, 6 });
    };
    anim(ANIM);

    anim(NPC);
    PutFloat(data, NPC + 0x14, 1.0f);
    PutFloat(data, NPC + 0x18, 6.0f);
    PutBE32(data, NPC + 0x1C, 2);
    PutFloat(data, NPC + 0x20, -5.0f);
    ptr(NPC + 0x24, EYE);
    Put16s(data, NPC + 0x28, { 1, -1 });
    Put16s(data, NPC + 0x30, { 0, -1, 3, 4 });
    ptr(NPC + 0x38, FEEL);
    ptr(NPC + 0x3C, SE_TABLE);
    for (int i = 0; i < 6; i++) {
        data[EYE + i] = (uint8_t)(i % 3);
    }
    Put16s(data, FEEL, { 2 });
    data[FEEL + 2] = 20;
    data[FEEL + 3] = 2;
    ptr(FEEL + 4, FEEL_SET);
    PutWords(data, FEEL_SET, { 5, 9 });
    ptr(SE_TABLE, SE_DATA);
    PutBE32(data, SE_TABLE + 8, 1);
    ptr(SE_TABLE + 12, SE_OTHER);
    PutBE32(data, SE_DATA, 2);
    ptr(SE_DATA + 4, SE_FRAMES);
    PutWords(data, SE_FRAMES, { 3, 7 });
    PutBE32(data, SE_OTHER, 1);
    ptr(SE_OTHER + 4, SE_FRAMES);
    PutBE16(data, SE_OTHER + 8, 290);

    data[SCROLL] = 1;
    data[SCROLL + 1] = 0xFF;
    data[SCROLL + 2] = 32;
    data[SCROLL + 3] = 16;
    data[SCROLL + 4] = 2;
    data[SCROLL + 5] = 0xFE;
    data[SCROLL + 6] = 64;
    data[SCROLL + 7] = 8;
    Put16s(data, COLREG, { 30, 2 });
    ptr(COLREG + 4, PRIM);
    ptr(COLREG + 8, ENV);
    ptr(COLREG + 12, COL_KEYS);
    for (int i = 0; i < 10; i++) {
        data[PRIM + i] = (uint8_t)(0x10 + i);
    }
    for (int i = 0; i < 8; i++) {
        data[ENV + i] = (uint8_t)(0x40 + i);
    }
    Put16s(data, COL_KEYS, { 0, 15 });
    Put16s(data, TEXANIME, { 4, 0 });
    ptr(TEXANIME + 4, TEX_TABLE);
    ptr(TEXANIME + 8, PATTERN);
    ptr(TEX_TABLE, TEX_A);
    ptr(TEX_TABLE + 4, TEX_B);
    data[PATTERN] = 0;
    data[PATTERN + 1] = 1;
    data[PATTERN + 2] = 1;
    data[PATTERN + 3] = 0;
    data[EVW] = 1;
    PutBE16(data, EVW + 2, 1);
    ptr(EVW + 4, SCROLL);
    data[EVW + 8] = 2;
    PutBE16(data, EVW + 10, 3);
    ptr(EVW + 12, COLREG);
    data[EVW + 16] = (uint8_t)-3;
    PutBE16(data, EVW + 18, 5);
    ptr(EVW + 20, TEXANIME);

    d.texA = Pattern(32, 32);
    d.texB = Pattern(32, 33);
    auto put = [&](uint32_t at, const std::vector<uint8_t>& bytes) { std::copy(bytes.begin(), bytes.end(), data.begin() + at); };
    put(TEX_A, TileC4(d.texA, 8, 8));
    put(TEX_B, TileC4(d.texB, 8, 8));
    d.atlas0 = Pattern(32, 34);
    d.atlas1 = Pattern(32, 35);
    put(ATLAS, TileC4(d.atlas0, 8, 8));
    put(ATLAS + 0x20, TileC4(d.atlas1, 8, 8));
    for (int i = 0; i < 16; i++) {
        PutBE16(data, PAL_ALPHA + i * 2, (uint16_t)((i % 8) << 12 | (i << 8) | (15 - i) << 4 | 3));
    }
    d.palAlpha.assign(data.begin() + PAL_ALPHA, data.begin() + PAL_ALPHA + 0x20);

    PutWords(data, DL_TARGET, { 0xFA000000, 1, 0xDF000000, 0, 0xFA000000, 2, 0xFB000000, 3, 0xDF000000, 0 });
    PutWords(data, DL_MAIN,
             {
                 0xDE000000, 0x0B000010,
                 0xF0000000 | (2 << 22) | (15 << 16) | 16, 0,
                 0xFD000000 | (2 << 21) | (0 << 19) | (1 << 18) | ((16 / 4 - 1) << 10) | (16 - 1), 0,
                 0xD2000000 | (15 << 12), 0,
                 0xDF000000, 0,
             });
    ptr(DL_MAIN + 0x0C, BSS_PAL, BSS_SECTION);
    ptr(DL_MAIN + 0x14, BSS_TEX + 0x40, BSS_SECTION);

    std::sort(relocs.begin(), relocs.end(), [](const Reloc& a, const Reloc& b) { return a.at < b.at; });
    std::vector<GC::RelRelocation> list = { Rel(0, GC::R_DOLPHIN_SECTION, DATA_SECTION, 0) };
    uint32_t at = 0;
    for (const auto& r : relocs) {
        list.push_back(Rel((uint16_t)(r.at - at), GC::R_PPC_ADDR32, r.section, r.target));
        at = r.at;
    }
    list.push_back(Rel(0, GC::R_DOLPHIN_END, 0, 0));

    Synthetic::RelSpec spec;
    spec.id = 1;
    spec.sections = { { false, {}, 0 },
                      { true, std::vector<uint8_t>(0x20, 0x60), 0 },
                      { false, {}, 0 },
                      { false, {}, 0 },
                      { false, Pattern(0x10, 36), 0 },
                      { false, data, 0 },
                      { false, {}, 0x100 } };
    spec.imports = { { 1, list } };
    const auto rel = Synthetic::BuildRel(spec);
    d.relSha1 = GC::Sha1Hex(rel);

    d.fg.resize(0x206 * 2);
    for (size_t i = 0; i < d.fg.size(); i++) {
        d.fg[i] = (uint8_t)(i * 7);
    }
    std::vector<uint8_t> table;
    for (uint32_t v : { 0x10u, 0x20u, 0x35u, 0x1000u }) {
        AppendBE32(table, v);
    }
    d.item0 = Pattern(32, 37);
    std::vector<uint8_t> items = TileC4(d.item0, 8, 8);
    const auto item1 = TileC4(Pattern(32, 38), 8, 8);
    items.insert(items.end(), item1.begin(), item1.end());
    for (int i = 0; i < 16; i++) {
        items.push_back(0x80 | (uint8_t)i);
        items.push_back((uint8_t)i);
    }
    const auto arc = Synthetic::BuildArc({ { "ROOT", "bin1",
                                             { { "fg.bin", 0x11, 0, d.fg },
                                               { "tbl.bin", 0x11, 0, table },
                                               { "items.bin", 0x11, 0, items },
                                               { ".", 0x02, 0, {} },
                                               { "..", 0x02, 0xFFFFFFFF, {} } } } });

    WriteFile(d.disc / "sys" / "main.dol", d.dol);
    WriteFile(d.disc / "files" / "foresta.rel.szs", Yaz0Literal(rel));
    WriteFile(d.disc / "files" / "audiorom.img", Pattern(0x100, 39));
    WriteFile(d.disc / "files" / "forest_1st.arc", arc);

    WriteText(d.src / "assets" / "ac.yml",
              "dl_joint: { type: GFX, offset: 0x81000000, size: 8 }\n"
              "skel: { type: AC:CKF_SKELETON, offset: 0x81000028, counts: { joints: 2 } }\n"
              "anim: { type: AC:CKF_ANIMATION, offset: 0x81000070, joints: 2, counts: { flags: 2, data: 18, keys: 5, "
              "fixed: 4 } }\n"
              "npc: { type: AC:NPC_ANIM, offset: 0x81000084, joints: 2, counts: { eye_seq: 6 } }\n"
              "evw: { type: AC:EVW_ANIME, offset: 0x81000158 }\n"
              "tex_a: { type: TEXTURE, offset: 0x81000180, format: CI4, width: 8, height: 8, source_encoding: gx }\n"
              "tex_b: { type: TEXTURE, offset: 0x810001A0, format: CI4, width: 8, height: 8, source_encoding: gx }\n"
              "atlas: { type: AC:TEX_ATLAS, offset: 0x810001C0, size: 0x40, regions: [ { offset: 0, format: CI4, "
              "width: 8, height: 8 }, { offset: 0x20, format: CI4, width: 8, height: 8 } ] }\n"
              "pal_alpha: { type: TEXTURE, offset: 0x81000200, format: TLUT, colors: 16, source_encoding: gx }\n"
              "dl_target: { type: GFX, offset: 0x81000220, size: 0x28 }\n"
              "dl_main: { type: GFX, offset: 0x81000248, size: 0x28, hints: { dl_index: [ { segment: 0x0B, targets: "
              "[0x81000220] } ] } }\n");
    WriteText(d.src / "assets" / "bss.yml",
              "tex_buf: { type: AC:RUNTIME_BUFFER, offset: 0x82100000, size: 0x80, segment: 0x0F, segment_offset: 0 }\n"
              "pal_buf: { type: AC:RUNTIME_BUFFER, offset: 0x82100080, size: 0x20, segment: 0x0F, segment_offset: "
              "0x80 }\n");
    WriteText(d.src / "assets" / "arc.yml",
              ":config:\n"
              "  directory: items\n"
              "fg: { type: AC:FG_DATA, file: forest_1st.arc/fg.bin }\n"
              "tbl: { type: AC:STRUCT_ARRAY, file: forest_1st.arc/tbl.bin, fields: [u32] }\n"
              "actable: { type: AC:STRUCT_ARRAY, file: forest_1st.arc/fg.bin, size: 0x30, fields: [u16, pad*2, s32*2, "
              "u8*2, s8, pad, s16, pad*2, u32] }\n"
              "tex/0: { type: TEXTURE, file: forest_1st.arc/items.bin, file_offset: 0x0, format: CI4, width: 8, "
              "height: 8, source_encoding: gx }\n"
              "tex/1: { type: TEXTURE, file: forest_1st.arc/items.bin, file_offset: 0x20, format: CI4, width: 8, "
              "height: 8, source_encoding: gx }\n"
              "pal: { type: TEXTURE, file: forest_1st.arc/items.bin, file_offset: 0x40, format: TLUT, colors: 16, "
              "source_encoding: gx }\n"
              "index: { type: AC:ASSET_ARRAY, entries: [items/tex/0, items/tex/1, null] }\n");
    return d;
}

void WriteConfig(const Disc& d) {
    WriteText(d.src / "config.yml",
              GC::Sha1Hex(d.dol) +
                  ":\n"
                  "  name: Synthetic AC disc\n"
                  "  path: assets\n"
                  "  gamecube:\n"
                  "    dol: { file: sys/main.dol, segment: 0x80 }\n"
                  "    rel:\n"
                  "      - file: files/foresta.rel.szs\n"
                  "        sha1: " +
                  d.relSha1 +
                  "\n"
                  "        sections: { .data: 0x81000000, .rodata: 0x82000000, .bss: 0x82100000, .text: 0x82400000 }\n"
                  "    files:\n"
                  "      - { file: files/audiorom.img, segment: 0x83 }\n"
                  "      - { file: files/forest_1st.arc, type: rarc, segment: 0x84 }\n"
                  "  config:\n"
                  "    gbi: F3DEX2_AC\n"
                  "    gbi_floats: false\n"
                  "    strict_declarations: true\n"
                  "    global_addr_space: true\n"
                  "    logging: ERROR\n"
                  "    output: { binary: ac.o2r }\n");
}

void RunTorch(const Disc& d) {
    const auto cwd = fs::current_path();
    fs::current_path(d.root);
    try {
        Companion::Instance = new Companion(d.disc, ArchiveType::O2R, true, d.src.string(), d.out.string());
        Companion::Instance->Init(ExportType::Binary);
    } catch (...) {
        fs::current_path(cwd);
        throw;
    }
    fs::current_path(cwd);
}

class Payload {
public:
    explicit Payload(std::vector<uint8_t> file, size_t start = 0x40) : mData(std::move(file)), mPos(start) {}
    uint8_t U8() { return mData.at(mPos++); }
    int16_t S16() {
        const auto v = (int16_t)Native16(mData, mPos);
        mPos += 2;
        return v;
    }
    uint16_t U16() { return (uint16_t)S16(); }
    uint32_t U32() {
        const auto v = Native32(mData, mPos);
        mPos += 4;
        return v;
    }
    float F32() {
        const uint32_t v = U32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    std::string Str() {
        const uint32_t n = U32();
        std::string s(mData.begin() + mPos, mData.begin() + mPos + n);
        mPos += n;
        return s;
    }
    bool AtEnd() const { return mPos == mData.size(); }
    const std::vector<uint8_t>& Data() const { return mData; }

private:
    std::vector<uint8_t> mData;
    size_t mPos;
};

uint32_t ResourceType(const std::vector<uint8_t>& file) {
    return Native32(file, 4);
}

class ACFactories : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        sDisc = new Disc(MakeDisc("ac_factories"));
        WriteConfig(*sDisc);
        RunTorch(*sDisc);
    }
    static void TearDownTestSuite() {
        fs::remove_all(sDisc->root);
        delete sDisc;
    }
    static std::vector<uint8_t> Read(const std::string& path) {
        return ReadFile(sDisc->root / "debug" / path);
    }
    static Disc* sDisc;
};

Disc* ACFactories::sDisc = nullptr;

} // namespace

TEST(ACKeyFrame, CountsFollowCKeyframe) {
    const auto c = AC::CountKeyFrameTables({ 0x3C, 0x01 });
    EXPECT_EQ(c.keys, 5u) << "root translation bits only count on joint 0";
    EXPECT_EQ(c.fixed, 4u);
    const auto other = AC::CountKeyFrameTables({ 0x07, 0x38, 0x00 });
    EXPECT_EQ(other.keys, 3u) << "translation bits on other joints are ignored";
    EXPECT_EQ(other.fixed, 3u + 3 * 3 - 3);
}

TEST_F(ACFactories, SkeletonIs2S2HKeyFrameSkel) {
    Payload p(Read("ac/skel"));
    EXPECT_EQ(ResourceType(p.Data()), 0x4F4B4653u);
    EXPECT_EQ(p.U8(), 2);
    EXPECT_EQ(p.U8(), 1);
    EXPECT_EQ(p.U8(), 0);
    EXPECT_EQ(p.U8(), 2);
    EXPECT_EQ(p.Str(), "ac/dl_joint");
    EXPECT_EQ(p.U8(), 1);
    EXPECT_EQ(p.U8(), 0);
    EXPECT_EQ(p.S16(), 1);
    EXPECT_EQ(p.S16(), 2);
    EXPECT_EQ(p.S16(), 3);
    EXPECT_EQ(p.Str(), "");
    EXPECT_EQ(p.U8(), 0);
    EXPECT_EQ(p.U8(), 1);
    EXPECT_EQ(p.S16(), -4);
    EXPECT_EQ(p.S16(), 5);
    EXPECT_EQ(p.S16(), -6);
    EXPECT_TRUE(p.AtEnd());
}

void ExpectAnimBody(Payload& p) {
    EXPECT_EQ(p.U8(), 0);
    ASSERT_EQ(p.U32(), 2u);
    EXPECT_EQ(p.U8(), 0x3C);
    EXPECT_EQ(p.U8(), 0x01);
    ASSERT_EQ(p.U32(), 6u) << "sum of the key table";
    for (int i = 0; i < 18; i++) {
        EXPECT_EQ(p.S16(), i * 3 - 7);
    }
    ASSERT_EQ(p.U32(), 5u);
    for (int16_t k : { 1, 2, 1, 1, 1 }) {
        EXPECT_EQ(p.S16(), k);
    }
    ASSERT_EQ(p.U32(), 4u);
    for (int16_t v : { 10, -20, 30, -40 }) {
        EXPECT_EQ(p.S16(), v);
    }
    EXPECT_EQ(p.S16(), -1);
    EXPECT_EQ(p.S16(), 6);
}

TEST_F(ACFactories, AnimationIs2S2HKeyFrameAnim) {
    Payload p(Read("ac/anim"));
    EXPECT_EQ(ResourceType(p.Data()), 0x4F4B4641u);
    ExpectAnimBody(p);
    EXPECT_TRUE(p.AtEnd());
}

TEST_F(ACFactories, NpcAnimationCarriesItsTables) {
    Payload p(Read("ac/npc"));
    EXPECT_EQ(ResourceType(p.Data()), 0x4F4E5041u);
    ExpectAnimBody(p);
    EXPECT_EQ(p.F32(), 1.0f);
    EXPECT_EQ(p.F32(), 6.0f);
    EXPECT_EQ(p.U32(), 2u);
    EXPECT_EQ(p.F32(), -5.0f);
    ASSERT_EQ(p.U32(), 6u) << "one eye pattern entry per frame";
    for (int i = 0; i < 6; i++) {
        EXPECT_EQ(p.U8(), i % 3);
    }
    EXPECT_EQ(p.S16(), 1);
    EXPECT_EQ(p.S16(), -1);
    EXPECT_EQ(p.U32(), 0u) << "NULL mouth sequence";
    EXPECT_EQ(p.S16(), 0);
    EXPECT_EQ(p.S16(), -1);
    EXPECT_EQ(p.S16(), 3);
    EXPECT_EQ(p.S16(), 4);
    ASSERT_EQ(p.U8(), 1);
    EXPECT_EQ(p.S16(), 2);
    EXPECT_EQ(p.U8(), 20);
    EXPECT_EQ(p.U8(), 2);
    ASSERT_EQ(p.U32(), 2u);
    EXPECT_EQ(p.U32(), 5u);
    EXPECT_EQ(p.U32(), 9u);
    ASSERT_EQ(p.U8(), 1);
    ASSERT_EQ(p.U8(), 1) << "lfoot";
    EXPECT_EQ(p.U32(), 2u);
    ASSERT_EQ(p.U32(), 2u);
    EXPECT_EQ(p.U32(), 3u);
    EXPECT_EQ(p.U32(), 7u);
    EXPECT_EQ(p.U8(), 0) << "rfoot is NULL";
    EXPECT_EQ(p.U32(), 1u);
    ASSERT_EQ(p.U32(), 1u);
    ASSERT_EQ(p.U8(), 1);
    EXPECT_EQ(p.U32(), 1u);
    ASSERT_EQ(p.U32(), 1u);
    EXPECT_EQ(p.U32(), 3u);
    EXPECT_EQ(p.U16(), 290);
    EXPECT_TRUE(p.AtEnd());
}

TEST_F(ACFactories, EvwAnimeIsTextureAnimationWithAcType5) {
    Payload p(Read("ac/evw"));
    EXPECT_EQ(ResourceType(p.Data()), 0x4F54414Eu);
    ASSERT_EQ(p.U32(), 3u);
    EXPECT_EQ((int8_t)p.U8(), 1);
    EXPECT_EQ(p.S16(), 1);
    for (uint8_t b : { 1, 0xFF, 32, 16, 2, 0xFE, 64, 8 }) {
        EXPECT_EQ(p.U8(), b);
    }
    EXPECT_EQ((int8_t)p.U8(), 2);
    EXPECT_EQ(p.S16(), 3);
    EXPECT_EQ(p.U16(), 30);
    EXPECT_EQ(p.U16(), 2);
    ASSERT_EQ(p.U16(), 2);
    EXPECT_EQ(p.U16(), 0);
    EXPECT_EQ(p.U16(), 15);
    ASSERT_EQ(p.U16(), 2) << "linear colour registers hold key_count colours";
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(p.U8(), 0x10 + i);
    }
    ASSERT_EQ(p.U16(), 2);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(p.U8(), 0x40 + i);
    }
    EXPECT_EQ((int8_t)p.U8(), -3);
    EXPECT_EQ(p.S16(), 5);
    EXPECT_EQ(p.U16(), 4);
    EXPECT_EQ(p.U16(), 0);
    ASSERT_EQ(p.U32(), 2u);
    EXPECT_EQ(p.Str(), "ac/tex_a");
    EXPECT_EQ(p.Str(), "ac/tex_b");
    ASSERT_EQ(p.U32(), 4u);
    for (uint8_t b : { 0, 1, 1, 0 }) {
        EXPECT_EQ(p.U8(), b);
    }
    EXPECT_EQ(p.U32(), 0u);
    EXPECT_TRUE(p.AtEnd());
}

TEST_F(ACFactories, AtlasRegionsAreConvertedInPlace) {
    const auto file = Read("ac/atlas");
    EXPECT_EQ(ResourceType(file), 0x4F544558u);
    EXPECT_EQ(Native32(file, 0x40), 3u) << "CI4";
    EXPECT_EQ(Native32(file, 0x44), 128u) << "texel count";
    EXPECT_EQ(Native32(file, 0x48), 1u);
    ASSERT_EQ(Native32(file, 0x4C), 0x40u);
    EXPECT_EQ(std::vector<uint8_t>(file.begin() + 0x50, file.begin() + 0x70), sDisc->atlas0);
    EXPECT_EQ(std::vector<uint8_t>(file.begin() + 0x70, file.begin() + 0x90), sDisc->atlas1);
}

TEST_F(ACFactories, PartialAlphaTlutIsRgba32) {
    const auto file = Read("ac/pal_alpha");
    EXPECT_EQ(Native32(file, 0x40), 1u) << "RGBA32";
    EXPECT_EQ(Native32(file, 0x44), 16u);
    EXPECT_EQ(Native32(file, 0x48), 1u);
    ASSERT_EQ(Native32(file, 0x4C), 64u);
    for (int i = 0; i < 16; i++) {
        uint8_t rgba[4];
        GX::Rgb5a3ToRgba8(GetBE16(sDisc->palAlpha, i * 2), rgba);
        for (int c = 0; c < 4; c++) {
            EXPECT_EQ(file[0x50 + i * 4 + c], rgba[c]) << "entry " << i << " channel " << c;
        }
    }
}

TEST_F(ACFactories, RuntimeBuffersAndDlIndexInDisplayLists) {
    EXPECT_FALSE(fs::exists(sDisc->root / "debug" / "bss" / "tex_buf")) << "runtime buffers export nothing";
    const auto file = Read("ac/dl_main");
    const uint32_t count = Native32(file, 0x48);
    std::vector<uint32_t> words;
    for (uint32_t i = 0; i < count * 2; i++) {
        words.push_back(Native32(file, 0x50 + i * 4));
    }
    ASSERT_GE(words.size(), 6u);
    EXPECT_EQ(words[4], 0x3D000000u) << "G_DL_INDEX";
    EXPECT_EQ(words[5], 0x0B000000u | (2 + 0x10 / 8)) << "command 2 of the target, after the 2-command prefix";
    EXPECT_EQ(words[6], 0xFD000000u | (0 << 21) | (2 << 19)) << "TLUT image from the buffer";
    EXPECT_EQ(words[7], 0x0F000081u);
    bool texFound = false;
    for (size_t i = 0; i + 1 < words.size(); i += 2) {
        if ((words[i] >> 24) == 0xFD && words[i + 1] == 0x0F000041u) {
            texFound = true;
        }
    }
    EXPECT_TRUE(texFound) << "texture image at buffer + 0x40 on segment 0x0F";
}

TEST_F(ACFactories, ArchiveItems) {
    const auto fg = Read("items/fg");
    EXPECT_EQ(ResourceType(fg), 0x47415252u);
    EXPECT_EQ(Native32(fg, 0x40), 0u) << "u8 elements";
    ASSERT_EQ(Native32(fg, 0x44), sDisc->fg.size());
    for (size_t r = 0; r < 2; r++) {
        const size_t base = r * 0x206;
        for (size_t k = 0; k < 257; k++) {
            EXPECT_EQ(fg[0x48 + base + k * 2], sDisc->fg[base + k * 2 + 1]) << "u16 fields are byte swapped";
            EXPECT_EQ(fg[0x48 + base + k * 2 + 1], sDisc->fg[base + k * 2]);
        }
        for (size_t k = 0; k < 4; k++) {
            EXPECT_EQ(fg[0x48 + base + 0x202 + k], sDisc->fg[base + 0x202 + k]) << "u8 fields are not";
        }
    }

    const auto tbl = Read("items/tbl");
    EXPECT_EQ(Native32(tbl, 0x40), 4u) << "u32 elements";
    ASSERT_EQ(Native32(tbl, 0x44), 4u);
    EXPECT_EQ(Native32(tbl, 0x48), 0x10u);
    EXPECT_EQ(Native32(tbl, 0x54), 0x1000u);

    const auto actable = Read("items/actable");
    EXPECT_EQ(Native32(actable, 0x40), 0u) << "u8 elements";
    ASSERT_EQ(Native32(actable, 0x44), 0x30u);
    const std::vector<std::pair<size_t, size_t>> layout = { { 0, 2 }, { 2, 1 },  { 3, 1 },  { 4, 4 },  { 8, 4 },
                                                            { 12, 1 }, { 13, 1 }, { 14, 1 }, { 15, 1 }, { 16, 2 },
                                                            { 18, 1 }, { 19, 1 }, { 20, 4 } };
    for (size_t r = 0; r < 2; r++) {
        for (const auto& [off, width] : layout) {
            for (size_t b = 0; b < width; b++) {
                EXPECT_EQ(actable[0x48 + r * 24 + off + b], sDisc->fg[r * 24 + off + width - 1 - b])
                    << "record " << r << " byte " << off + b;
            }
        }
    }

    const auto tex = Read("items/tex/0");
    EXPECT_EQ(std::vector<uint8_t>(tex.begin() + 0x50, tex.end()), sDisc->item0);
    const auto pal = Read("items/pal");
    EXPECT_EQ(Native32(pal, 0x44), 16u);

    const auto index = Read("items/index");
    EXPECT_EQ(ResourceType(index), 0x41415252u);
    ASSERT_EQ(Native32(index, 0x40), 3u);
    EXPECT_EQ(Native64(index, 0x44), CRC64("items/tex/0"));
    EXPECT_EQ(Native64(index, 0x4C), CRC64("items/tex/1"));
    EXPECT_EQ(Native64(index, 0x54), 0u);

    for (const auto& path : Companion::Manifest(sDisc->src / "assets", {})) {
        EXPECT_TRUE(fs::exists(sDisc->root / "debug" / path)) << path;
    }
}

TEST(ACFactoryErrors, ItemPastTheEndOfAMemberIsRejected) {
    auto d = MakeDisc("ac_factories_overrun");
    WriteConfig(d);
    WriteText(d.src / "assets" / "arc.yml",
              ":config:\n"
              "  directory: items\n"
              "bad: { type: BLOB, file: forest_1st.arc/tbl.bin, file_offset: 0x8, size: 0x10 }\n");
    EXPECT_THROW(RunTorch(d), std::runtime_error);
    fs::remove_all(d.root);
}
