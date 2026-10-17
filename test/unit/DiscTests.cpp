#include <gtest/gtest.h>

#include "SyntheticDisc.h"
#include "TestUtils.h"
#include "input/gc/BlockReader.h"
#include "input/gc/DolFile.h"
#include "input/gc/GameCubeInput.h"
#include "input/gc/GcmDisc.h"

#include <cstdlib>
#include <iostream>

#if __has_include(<zlib.h>)
#include <zlib.h>
#define TEST_GCZ
#endif

using namespace GC;
using namespace TestUtils;
using Synthetic::Rel;

namespace {

struct TestDisc {
    std::vector<uint8_t> dol;
    std::vector<uint8_t> rel;
    std::vector<uint8_t> relSzs;
    std::vector<uint8_t> audio;
    std::vector<uint8_t> member0;
    std::vector<uint8_t> member1;
    std::map<std::string, std::vector<uint8_t>> files;
};

TestDisc MakeDisc() {
    TestDisc d;
    d.dol = Synthetic::BuildDol({ { 0x80003100, Pattern(0x40, 1) } }, { { 0x80005000, Pattern(0x20, 2) } },
                                0x80005020, 0x100);

    std::vector<uint8_t> data(0x40, 0);
    Synthetic::RelSpec spec;
    spec.id = 1;
    spec.sections = { { false, {}, 0 },
                      { true, std::vector<uint8_t>(0x20, 0x60), 0 },
                      { false, {}, 0 },
                      { false, {}, 0 },
                      { false, Pattern(0x10, 3), 0 },
                      { false, data, 0 },
                      { false, {}, 0x40 } };
    spec.imports = {
        { 1,
          { Rel(0, R_DOLPHIN_SECTION, 5, 0), Rel(0x00, R_PPC_ADDR32, 5, 0x20), Rel(0x04, R_PPC_ADDR32, 4, 0x4),
            Rel(0x04, R_PPC_ADDR32, 6, 0x10), Rel(0, R_DOLPHIN_END, 0, 0) } },
        { 0, { Rel(0, R_DOLPHIN_SECTION, 5, 0), Rel(0x0C, R_PPC_ADDR32, 0, 0x80005004), Rel(0, R_DOLPHIN_END, 0, 0) } },
    };
    d.rel = Synthetic::BuildRel(spec);
    d.relSzs = Yaz0Literal(d.rel);
    d.audio = Pattern(0x300, 4);
    d.member0 = Pattern(0x45, 5);
    d.member1 = Pattern(0x80, 6);
    const auto arc = Synthetic::BuildArc({ { "ROOT",
                                             "forest_1st",
                                             { { "fgdata.bin", 0x11, 0, d.member0 },
                                               { "tex", 0x02, 1, {} },
                                               { ".", 0x02, 0, {} },
                                               { "..", 0x02, 0xFFFFFFFF, {} } } },
                                           { "TEX ",
                                             "tex",
                                             { { "room.bin", 0x95, 0, Yaz0Literal(d.member1) },
                                               { ".", 0x02, 1, {} },
                                               { "..", 0x02, 0, {} } } } });
    d.files = { { "foresta.rel.szs", d.relSzs }, { "audiorom.img", d.audio }, { "forest_1st.arc", arc } };
    return d;
}

YAML::Node MakeConfig(const std::string& relSha1) {
    return YAML::Load("gamecube:\n"
                      "  dol: { file: sys/main.dol, segment: 0x80 }\n"
                      "  rel:\n"
                      "    - file: files/foresta.rel.szs\n"
                      "      sha1: " +
                      relSha1 +
                      "\n"
                      "      sections: { .data: 0x81000000, .rodata: 0x82000000, .bss: 0x82100000, .text: 0x82400000 }\n"
                      "  files:\n"
                      "    - { file: files/audiorom.img, segment: 0x83 }\n"
                      "    - { file: files/forest_1st.arc, type: rarc, segment: 0x84 }\n");
}

uint32_t At(GameCubeInput& input, uint32_t addr) {
    const uint32_t base = input.GetVirtualSegments().at(addr >> 24);
    return GetBE32(input.GetImage(), base + (addr & 0xFFFFFF));
}

void CheckBuiltImage(GameCubeInput& input, const TestDisc& d) {
    EXPECT_EQ(input.GetHash(), Sha1Hex(d.dol));
    EXPECT_EQ(input.GetVersionCRC(), std::stoul(Sha1Hex(d.dol).substr(0, 8), nullptr, 16));

    const auto& segs = input.GetVirtualSegments();
    for (uint32_t s : { 0x80u, 0x81u, 0x82u, 0x83u, 0x84u }) {
        ASSERT_TRUE(segs.contains(s)) << "segment " << s;
        EXPECT_EQ(segs.at(s) % 0x20, 0u);
    }

    const auto& image = input.GetImage();
    const uint32_t dolBase = segs.at(0x80);
    EXPECT_EQ(std::vector<uint8_t>(image.begin() + dolBase + 0x3100, image.begin() + dolBase + 0x3140),
              Pattern(0x40, 1));
    EXPECT_EQ(At(input, 0x80005020), 0u) << "DOL bss is zero";

    EXPECT_EQ(At(input, 0x81000000), 0x81000020u);
    EXPECT_EQ(At(input, 0x81000004), 0x82000004u);
    EXPECT_EQ(At(input, 0x81000008), 0x82100010u);
    EXPECT_EQ(At(input, 0x8100000C), 0x80005004u);
    EXPECT_EQ(At(input, 0x82400000), 0x60606060u);
    EXPECT_EQ(At(input, 0x82000000), GetBE32(Pattern(0x10, 3), 0));

    EXPECT_EQ(At(input, 0x83000000), GetBE32(d.audio, 0));
    EXPECT_EQ(input.GetFileAddresses().at("files/audiorom.img"), 0x83000000u);
    EXPECT_EQ(input.GetFileSizes().at("audiorom.img"), d.audio.size());

    EXPECT_EQ(input.GetFileAddresses().at("forest_1st.arc/fgdata.bin"), 0x84000000u);
    EXPECT_EQ(input.GetFileAddresses().at("files/forest_1st.arc/tex/room.bin"), 0x84000060u);
    EXPECT_EQ(input.GetFileSizes().at("forest_1st.arc/tex/room.bin"), d.member1.size());
    const uint32_t member = input.GetFileOffsets().at("forest_1st.arc/tex/room.bin");
    EXPECT_EQ(std::vector<uint8_t>(image.begin() + member, image.begin() + member + d.member1.size()), d.member1);
}

} // namespace

TEST(DolFile, MapsSectionsAndBss) {
    const auto dol = Synthetic::BuildDol({ { 0x80003100, Pattern(0x40, 1) }, { 0x80004000, Pattern(0x20, 9) } },
                                         { { 0x80005000, Pattern(0x20, 2) } }, 0x80005020, 0x100);
    DolFile file(dol);
    ASSERT_EQ(file.GetSections().size(), 3u);
    EXPECT_TRUE(file.GetSections()[0].text);
    EXPECT_FALSE(file.GetSections()[2].text);
    EXPECT_EQ(file.GetStart(), 0x80003100u);
    EXPECT_EQ(file.GetEnd(), 0x80005120u);
    EXPECT_EQ(file.GetEntryPoint(), 0x80003100u);

    std::vector<uint8_t> window(file.GetEnd() - 0x80000000, 0);
    file.Map(window.data(), 0x80000000, static_cast<uint32_t>(window.size()));
    EXPECT_EQ(std::vector<uint8_t>(window.begin() + 0x4000, window.begin() + 0x4020), Pattern(0x20, 9));
    EXPECT_EQ(std::vector<uint8_t>(window.begin() + 0x5000, window.begin() + 0x5020), Pattern(0x20, 2));
    EXPECT_EQ(DolFileSize(dol.data()), dol.size());
}

TEST(GcmDisc, ReadsBootDolAndFst) {
    const auto d = MakeDisc();
    std::map<std::string, std::vector<uint8_t>> files = d.files;
    files["deep/er/x.bin"] = Pattern(0x10, 11);
    auto image = Synthetic::BuildGcm("GAFE01", 0, d.dol, files);

    GcmDisc disc(std::make_shared<MemoryBlockReader>(image));
    EXPECT_EQ(disc.GetGameId(), "GAFE01");
    EXPECT_EQ(disc.GetRevision(), 0);
    EXPECT_EQ(disc.ReadFile("sys/main.dol"), d.dol);
    EXPECT_EQ(disc.ReadFile("files/audiorom.img"), d.audio);
    EXPECT_EQ(disc.ReadFile("files/deep/er/x.bin"), Pattern(0x10, 11));
    EXPECT_TRUE(disc.Exists("files/foresta.rel.szs"));
    EXPECT_FALSE(disc.Exists("files/missing.bin"));
    EXPECT_THROW(disc.ReadFile("files/missing.bin"), std::runtime_error);
}

TEST(GameCubeInput, BuildsWindowsFromAGcmImage) {
    const auto d = MakeDisc();
    auto image = Synthetic::BuildGcm("GAFE01", 0, d.dol, d.files);
    auto input = GameCubeInput::FromMemory(image);
    ASSERT_NE(input, nullptr);
    input->Open();
    EXPECT_EQ(input->GetTitle(), "GAFE01");

    auto config = MakeConfig(Sha1Hex(d.rel));
    input->Build(config);
    EXPECT_EQ(input->GetRelHashSemantics().at("files/foresta.rel.szs"), "decompressed");
    CheckBuiltImage(*input, d);
}

TEST(GameCubeInput, ReadsCisoImages) {
    const auto d = MakeDisc();
    auto ciso = Synthetic::BuildCiso(Synthetic::BuildGcm("GAFE01", 1, d.dol, d.files), 0x8000);
    auto input = GameCubeInput::FromMemory(ciso);
    ASSERT_NE(input, nullptr);
    input->Open();

    auto config = MakeConfig(Sha1Hex(d.relSzs));
    input->Build(config);
    EXPECT_EQ(input->GetRelHashSemantics().at("files/foresta.rel.szs"), "compressed");
    CheckBuiltImage(*input, d);
}

#ifdef TEST_GCZ
static void PutLE(std::vector<uint8_t>& buf, uint64_t value, int bytes) {
    for (int i = 0; i < bytes; i++) {
        buf.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

static std::vector<uint8_t> BuildGcz(const std::vector<uint8_t>& image, uint32_t blockSize) {
    const uint32_t numBlocks = static_cast<uint32_t>((image.size() + blockSize - 1) / blockSize);
    std::vector<uint64_t> pointers;
    std::vector<uint8_t> data;
    for (uint32_t i = 0; i < numBlocks; i++) {
        std::vector<uint8_t> block(blockSize, 0);
        const size_t start = static_cast<size_t>(i) * blockSize;
        std::copy(image.begin() + start, image.begin() + std::min(image.size(), start + blockSize), block.begin());
        uLongf length = compressBound(blockSize);
        std::vector<uint8_t> packed(length);
        compress2(packed.data(), &length, block.data(), blockSize, 9);
        if (length < blockSize && i % 3 != 2) {
            pointers.push_back(data.size());
            data.insert(data.end(), packed.begin(), packed.begin() + length);
        } else {
            pointers.push_back(data.size() | (1ULL << 63));
            data.insert(data.end(), block.begin(), block.end());
        }
    }

    std::vector<uint8_t> out;
    PutLE(out, 0xB10BC001, 4);
    PutLE(out, 0, 4);
    PutLE(out, data.size(), 8);
    PutLE(out, static_cast<uint64_t>(numBlocks) * blockSize, 8);
    PutLE(out, blockSize, 4);
    PutLE(out, numBlocks, 4);
    for (auto p : pointers) {
        PutLE(out, p, 8);
    }
    for (uint32_t i = 0; i < numBlocks; i++) {
        PutLE(out, 0, 4);
    }
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

TEST(GameCubeInput, ReadsGczImages) {
    const auto d = MakeDisc();
    auto gcz = BuildGcz(Synthetic::BuildGcm("GAFE01", 0, d.dol, d.files), 0x4000);
    auto input = GameCubeInput::FromMemory(gcz);
    ASSERT_NE(input, nullptr);
    input->Open();
    auto config = MakeConfig(Sha1Hex(d.rel));
    input->Build(config);
    CheckBuiltImage(*input, d);
}
#endif

TEST(GameCubeInput, ReadsExtractedFolders) {
    const auto d = MakeDisc();
    const auto root = MakeTempDir("folder_disc");
    WriteFile(root / "sys" / "main.dol", d.dol);
    std::vector<uint8_t> boot(0x440, 0);
    boot[0] = 'G', boot[1] = 'A', boot[2] = 'F', boot[3] = 'E', boot[4] = '0', boot[5] = '1';
    WriteFile(root / "sys" / "boot.bin", boot);
    for (const auto& [name, data] : d.files) {
        WriteFile(root / "files" / name, data);
    }

    auto input = GameCubeInput::FromPath(root);
    ASSERT_NE(input, nullptr);
    input->Open();
    EXPECT_EQ(input->GetTitle(), "GAFE01");
    auto config = MakeConfig(Sha1Hex(d.rel));
    input->Build(config);
    CheckBuiltImage(*input, d);
    std::filesystem::remove_all(root);
}

TEST(GameCubeInput, RejectsAWrongRelHash) {
    const auto d = MakeDisc();
    auto image = Synthetic::BuildGcm("GAFE01", 0, d.dol, d.files);
    auto input = GameCubeInput::FromMemory(image);
    input->Open();
    auto config = MakeConfig("0000000000000000000000000000000000000000");
    EXPECT_THROW(input->Build(config), std::runtime_error);
}

TEST(GameCubeInput, IgnoresNonGameCubeData) {
    std::vector<uint8_t> rom(0x1000, 0);
    rom[0] = 0x80, rom[1] = 0x37, rom[2] = 0x12, rom[3] = 0x40;
    EXPECT_EQ(GameCubeInput::FromMemory(rom), nullptr);
    EXPECT_EQ(rom.size(), 0x1000u);
}

TEST(GameCubeInput, RetailDiscIfAvailable) {
    const char* path = std::getenv("TORCH_GC_DISC");
    if (path == nullptr) {
        GTEST_SKIP() << "TORCH_GC_DISC not set";
    }
    auto input = GameCubeInput::FromPath(path);
    ASSERT_NE(input, nullptr);
    input->Open();
    EXPECT_EQ(input->GetHash(), "2ae8f56e7791d37e165bd5900921f2269f9515bf");
    EXPECT_EQ(input->GetVersionCRC(), 0x2AE8F56Eu);

    auto config = YAML::Load("gamecube:\n"
                             "  dol: { file: sys/main.dol, segment: 0x80 }\n"
                             "  rel:\n"
                             "    - file: files/foresta.rel.szs\n"
                             "      sha1: c59d278ad8542bb05d6cbb632f60a0db05bef203\n"
                             "      sections: { .data: 0x81000000, .rodata: 0x82000000, .bss: 0x82100000,\n"
                             "                  .ctors: 0x82380000, .dtors: 0x82380100, .text: 0x82400000 }\n"
                             "  files:\n"
                             "    - { file: files/audiorom.img, segment: 0x83 }\n"
                             "    - { file: files/forest_1st.arc, type: rarc, segment: 0x84 }\n"
                             "    - { file: files/forest_2nd.arc, type: rarc, segment: 0x85 }\n"
                             "    - { file: files/famicom.arc, type: rarc, segment: 0x86 }\n");
    input->Build(config);
    EXPECT_EQ(input->GetRelHashSemantics().at("files/foresta.rel.szs"), "decompressed");
    EXPECT_EQ(input->GetVirtualSegments().size(), 7u);
    EXPECT_EQ(input->GetFileSizes().at("audiorom.img"), 0x7EA760u);

    std::map<std::string, int> members;
    for (const auto& [name, addr] : input->GetFileAddresses()) {
        if (name.rfind("files/", 0) == 0) {
            continue;
        }
        const auto slash = name.find('/');
        if (slash != std::string::npos) {
            members[name.substr(0, slash)]++;
        }
    }
    EXPECT_GT(members["forest_1st.arc"], 0);
    EXPECT_GT(members["forest_2nd.arc"], 0);
    EXPECT_GT(members["famicom.arc"], 0);
    for (const auto& [arc, count] : members) {
        std::cout << arc << ": " << count << " members\n";
    }
}
