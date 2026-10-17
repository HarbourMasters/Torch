#include <gtest/gtest.h>

#include "Companion.h"
#include "SyntheticDisc.h"
#include "TestUtils.h"
#include "gc/GXTexture.h"
#include "input/gc/GameCubeInput.h"
#include <strhash64/StrHash64.h>

#include <cstring>

namespace fs = std::filesystem;
using namespace TestUtils;
using Synthetic::Rel;

namespace {

constexpr uint32_t TEX_CI4 = 0x000;
constexpr uint32_t PAL = 0x040;
constexpr uint32_t TEX_RGBA = 0x060;
constexpr uint32_t VTX = 0x0A0;
constexpr uint32_t DL_B = 0x120;
constexpr uint32_t DL_A = 0x160;
constexpr uint32_t TEX_LOSSY = 0x1A0;
constexpr uint32_t DATA_SIZE = 0x1C0;

std::vector<uint8_t> Tile(uint32_t bw, uint32_t bh, uint32_t bpp, const std::vector<uint8_t>& linear, uint32_t w,
                          uint32_t h) {
    std::vector<uint8_t> out(GX::TiledSize(bpp == 4 ? GX::TexFormat::C4 : GX::TexFormat::RGB5A3, w, h), 0);
    const uint32_t tilesX = (w + bw - 1) / bw;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const size_t index = ((y / bh) * tilesX + x / bw) * bw * bh + (y % bh) * bw + (x % bw);
            const size_t src = static_cast<size_t>(y) * w + x;
            if (bpp == 4) {
                const uint8_t v = (src & 1) ? linear[src / 2] & 0xF : linear[src / 2] >> 4;
                out[index / 2] |= (index & 1) ? v : v << 4;
            } else {
                out[index * 2] = linear[src * 2];
                out[index * 2 + 1] = linear[src * 2 + 1];
            }
        }
    }
    return out;
}

uint32_t Native32(const std::vector<uint8_t>& buf, size_t offset) {
    uint32_t v;
    std::memcpy(&v, buf.data() + offset, 4);
    return v;
}

struct Disc {
    fs::path root;
    fs::path disc;
    fs::path src;
    fs::path out;
    std::vector<uint8_t> dol;
    std::vector<uint8_t> dolData;
    std::vector<uint8_t> ci4Linear;
    std::vector<uint16_t> palette;
    std::vector<uint8_t> rgbaLinear;
    std::vector<uint8_t> member;
    std::string relSha1;
};

void PutWords(std::vector<uint8_t>& data, uint32_t offset, const std::vector<uint32_t>& words) {
    for (size_t i = 0; i < words.size(); i++) {
        PutBE32(data, offset + i * 4, words[i]);
    }
}

Disc MakeDisc(const std::string& name) {
    Disc d;
    d.root = MakeTempDir(name);
    d.disc = d.root / "disc";
    d.src = d.root / "src";
    d.out = d.root / "out";

    d.dolData = Pattern(0x20, 21);
    d.dol = Synthetic::BuildDol({ { 0x80003100, Pattern(0x40, 20) } }, { { 0x80005000, d.dolData } }, 0x80005020, 0x40);

    std::vector<uint8_t> data(DATA_SIZE, 0);
    d.ci4Linear = Pattern(16 * 8 / 2, 22);
    const auto ci4 = Tile(8, 8, 4, d.ci4Linear, 16, 8);
    std::copy(ci4.begin(), ci4.end(), data.begin() + TEX_CI4);

    for (uint16_t i = 0; i < 16; i++) {
        d.palette.push_back(0x8000 | (i << 10) | (i << 5) | i);
        PutBE16(data, PAL + i * 2, d.palette.back());
    }

    d.rgbaLinear.resize(8 * 4 * 2);
    for (size_t i = 0; i < 32; i++) {
        PutBE16(d.rgbaLinear, i * 2, 0x8000 | ((i * 1237) & 0x7FFF));
    }
    const auto rgba = Tile(4, 4, 16, d.rgbaLinear, 8, 4);
    std::copy(rgba.begin(), rgba.end(), data.begin() + TEX_RGBA);

    const auto vtx = Pattern(0x80, 23);
    std::copy(vtx.begin(), vtx.end(), data.begin() + VTX);

    std::vector<uint8_t> lossy(4 * 4 * 2, 0x80);
    PutBE16(lossy, 0, 0x3ABC);
    const auto lossyTiled = Tile(4, 4, 16, lossy, 4, 4);
    std::copy(lossyTiled.begin(), lossyTiled.end(), data.begin() + TEX_LOSSY);

    PutWords(data, DL_B,
             {
                 0xF0000000 | (2 << 22) | (0 << 16) | (1 << 14) | 16, 0,
                 0xFD000000 | (2 << 21) | (0 << 19) | (1 << 18) | ((8 / 4 - 1) << 10) | (16 - 1), 0,
                 0xD2000000 | (15 << 20) | (0 << 16) | (0 << 12) | (1 << 10) | (0 << 8), 0,
                 0x01000000 | (4 << 12) | (4 << 1), 0,
                 0x0A000000 | (1 << 17) | ((1 | 2 << 5 | 3 << 10) >> 13), ((uint32_t)(1 | 2 << 5 | 3 << 10) << 19) | ((0 | 1 << 5 | 2 << 10) << 4),
                 0xDE000000, 0x08000000,
                 0xDE000000, 0,
                 0xDF000000, 0,
             });
    PutWords(data, DL_A,
             {
                 0xFD000000 | (0 << 21) | (2 << 19) | (1 << 18) | ((4 / 4 - 1) << 10) | (8 - 1), 0,
                 0xD2000000 | (15 << 20) | (0 << 16), 0,
                 0xFD000000 | (0 << 21) | (2 << 19) | (1 << 18) | ((4 / 4 - 1) << 10) | (4 - 1), 0,
                 0xD2000000 | (15 << 20) | (1 << 16), 0,
                 0xDF000000, 0,
             });

    Synthetic::RelSpec spec;
    spec.id = 1;
    spec.sections = { { false, {}, 0 },
                      { true, std::vector<uint8_t>(0x20, 0x60), 0 },
                      { false, {}, 0 },
                      { false, {}, 0 },
                      { false, Pattern(0x10, 24), 0 },
                      { false, data, 0 },
                      { false, {}, 0x40 } };
    spec.imports = { { 1,
                       { Rel(0, GC::R_DOLPHIN_SECTION, 5, 0), Rel(DL_B + 0x04, GC::R_PPC_ADDR32, 5, PAL),
                         Rel(0x08, GC::R_PPC_ADDR32, 5, TEX_CI4), Rel(0x10, GC::R_PPC_ADDR32, 5, VTX + 0x20),
                         Rel(0x18, GC::R_PPC_ADDR32, 5, DL_A), Rel(0x10, GC::R_PPC_ADDR32, 5, TEX_RGBA),
                         Rel(0x10, GC::R_PPC_ADDR32, 5, TEX_LOSSY), Rel(0, GC::R_DOLPHIN_END, 0, 0) } } };
    const auto rel = Synthetic::BuildRel(spec);
    d.relSha1 = GC::Sha1Hex(rel);

    d.member = Pattern(0x33, 25);
    const auto arc = Synthetic::BuildArc(
        { { "ROOT", "forest_1st", { { "fgdata.bin", 0x11, 0, d.member }, { ".", 0x02, 0, {} }, { "..", 0x02, 0xFFFFFFFF, {} } } } });

    WriteFile(d.disc / "sys" / "main.dol", d.dol);
    WriteFile(d.disc / "files" / "foresta.rel.szs", Yaz0Literal(rel));
    WriteFile(d.disc / "files" / "audiorom.img", Pattern(0x100, 26));
    WriteFile(d.disc / "files" / "forest_1st.arc", arc);

    WriteText(d.src / "assets" / "a.yml",
              "tex_ci4: { type: TEXTURE, offset: 0x81000000, format: CI4, width: 16, height: 8, source_encoding: gx }\n"
              "pal: { type: TEXTURE, offset: 0x81000040, format: TLUT, colors: 16, source_encoding: gx }\n"
              "tex_rgba: { type: TEXTURE, offset: 0x81000060, format: RGBA16, width: 8, height: 4, source_encoding: gx }\n"
              "vtx: { type: VTX, offset: 0x810000A0, count: 8 }\n"
              "dl_a: { type: GFX, offset: 0x81000160, size: 0x28 }\n"
              "tex_lossy: { type: TEXTURE, offset: 0x810001A0, format: RGBA16, width: 4, height: 4, source_encoding: gx }\n");
    WriteText(d.src / "assets" / "b.yml", "dl_b: { type: GFX, offset: 0x81000120, size: 0x40 }\n");
    WriteText(d.src / "assets" / "disc.yml", "fgdata: { type: BLOB, file: forest_1st.arc/fgdata.bin }\n"
                                             "dol_data: { type: BLOB, offset: 0x80005000, size: 0x20 }\n");
    return d;
}

void WriteConfig(const Disc& d, bool globalAddrSpace) {
    WriteText(d.src / "config.yml",
              GC::Sha1Hex(d.dol) +
                  ":\n"
                  "  name: Synthetic GameCube disc\n"
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
                  "    global_addr_space: " +
                  std::string(globalAddrSpace ? "true" : "false") +
                  "\n"
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

std::vector<uint32_t> DisplayListWords(const std::vector<uint8_t>& file) {
    EXPECT_EQ(Native32(file, 4), 0x4F444C54u);
    EXPECT_EQ(Native32(file, 8), 1u);
    EXPECT_EQ(file[0x40], 4);
    const uint32_t count = Native32(file, 0x48);
    EXPECT_EQ(file.size(), 0x50 + count * 8);
    std::vector<uint32_t> words;
    for (uint32_t i = 0; i < count * 2; i++) {
        words.push_back(Native32(file, 0x50 + i * 4));
    }
    return words;
}

size_t FindHash(const std::vector<uint32_t>& words, const std::string& path) {
    const uint64_t hash = CRC64(path.c_str());
    for (size_t i = 0; i + 1 < words.size(); i += 2) {
        if (words[i] == (uint32_t)(hash >> 32) && words[i + 1] == (uint32_t)hash) {
            return i;
        }
    }
    return std::string::npos;
}

} // namespace

TEST(GameCubeExtract, SyntheticDiscToO2R) {
    auto d = MakeDisc("extract");
    WriteConfig(d, true);
    ASSERT_NO_THROW(RunTorch(d));
    const auto debug = d.root / "debug";
    EXPECT_TRUE(fs::exists(d.out / "ac.o2r"));

    const auto version = ReadFile(debug / "version");
    ASSERT_EQ(version.size(), 5u);
    EXPECT_EQ(version[0], 1);
    EXPECT_EQ(GetBE32(version, 1), std::stoul(GC::Sha1Hex(d.dol).substr(0, 8), nullptr, 16));

    const auto ci4 = ReadFile(debug / "a" / "tex_ci4");
    EXPECT_EQ(Native32(ci4, 0x40), 3u);
    EXPECT_EQ(Native32(ci4, 0x44), 16u);
    EXPECT_EQ(Native32(ci4, 0x48), 8u);
    EXPECT_EQ(std::vector<uint8_t>(ci4.begin() + 0x50, ci4.end()), d.ci4Linear);

    const auto pal = ReadFile(debug / "a" / "pal");
    EXPECT_EQ(Native32(pal, 0x40), 2u);
    EXPECT_EQ(Native32(pal, 0x44), 16u);
    for (uint16_t i = 0; i < 16; i++) {
        EXPECT_EQ(GetBE16(pal, 0x50 + i * 2), (i << 11) | (i << 6) | (i << 1) | 1);
    }

    const auto rgba = ReadFile(debug / "a" / "tex_rgba");
    EXPECT_EQ(Native32(rgba, 0x40), 2u);
    for (size_t i = 0; i < 32; i++) {
        EXPECT_EQ(GetBE16(rgba, 0x50 + i * 2), GX::Rgb5a3ToRgba5551(GetBE16(d.rgbaLinear, i * 2)));
    }

    const auto lossy = ReadFile(debug / "a" / "tex_lossy");
    EXPECT_EQ(Native32(lossy, 0x40), 1u);
    EXPECT_EQ(Native32(lossy, 0x4C), 64u);

    const auto dlB = DisplayListWords(ReadFile(debug / "b" / "dl_b"));
    EXPECT_EQ(FindHash(dlB, "b/dl_b"), 2u) << "G_MARKER carries the DL's own path";
    const size_t palAt = FindHash(dlB, "a/pal");
    const size_t texAt = FindHash(dlB, "a/tex_ci4");
    const size_t vtxAt = FindHash(dlB, "a/vtx");
    const size_t dlAt = FindHash(dlB, "a/dl_a");
    ASSERT_NE(palAt, std::string::npos);
    ASSERT_NE(texAt, std::string::npos);
    ASSERT_NE(vtxAt, std::string::npos);
    ASSERT_NE(dlAt, std::string::npos);
    EXPECT_LT(palAt, texAt);
    EXPECT_LT(texAt, vtxAt);
    EXPECT_LT(vtxAt, dlAt);
    EXPECT_EQ(dlB[palAt - 2] >> 24, 0x20u);
    EXPECT_EQ(dlB[vtxAt - 2] >> 24, 0x32u);
    EXPECT_EQ(dlB[vtxAt - 1], 0x20u) << "interior vertex pointer keeps its byte offset";
    EXPECT_EQ(dlB[vtxAt + 2], 0x06000204u) << "TRIN becomes G_TRI2";
    EXPECT_EQ(dlB[vtxAt + 3], 0x00020406u);
    EXPECT_EQ(dlB[vtxAt + 4], 0xDE000000u);
    EXPECT_EQ(dlB[vtxAt + 5], 0x08000001u) << "runtime segment operands are tagged";
    EXPECT_EQ(dlB[dlB.size() - 2], 0xDF000000u);

    const auto dlA = DisplayListWords(ReadFile(debug / "a" / "dl_a"));
    const size_t lossyAt = FindHash(dlA, "a/tex_lossy");
    const size_t rgbaAt = FindHash(dlA, "a/tex_rgba");
    ASSERT_NE(lossyAt, std::string::npos);
    ASSERT_NE(rgbaAt, std::string::npos);
    EXPECT_EQ((dlA[rgbaAt - 2] >> 19) & 3, 2u);
    EXPECT_EQ((dlA[lossyAt - 2] >> 19) & 3, 3u) << "the promoted texture is loaded as 32-bit";

    const auto fg = ReadFile(debug / "disc" / "fgdata");
    EXPECT_EQ(Native32(fg, 0x40), d.member.size());
    EXPECT_EQ(std::vector<uint8_t>(fg.begin() + 0x44, fg.end()), d.member);

    const auto dolBlob = ReadFile(debug / "disc" / "dol_data");
    EXPECT_EQ(std::vector<uint8_t>(dolBlob.begin() + 0x44, dolBlob.end()), d.dolData);

    for (const auto& path : Companion::Manifest(d.src / "assets", {})) {
        EXPECT_TRUE(fs::exists(debug / path)) << path;
    }
    fs::remove_all(d.root);
}

TEST(GameCubeExtract, CrossFileReferencesNeedTheGlobalAddressSpace) {
    auto d = MakeDisc("extract_local");
    WriteConfig(d, false);
    EXPECT_THROW(RunTorch(d), std::runtime_error);
    fs::remove_all(d.root);
}

TEST(GameCubeExtract, PerFileStrictDeclarationsOverride) {
    auto d = MakeDisc("extract_override");
    WriteConfig(d, false);
    WriteText(d.src / "assets" / "b.yml", ":config:\n"
                                          "  strict_declarations: false\n"
                                          "dl_b: { type: GFX, offset: 0x81000120, size: 0x40 }\n");
    EXPECT_NO_THROW(RunTorch(d));
    EXPECT_TRUE(fs::exists(d.root / "debug" / "b" / "dl_b"));
    fs::remove_all(d.root);
}
