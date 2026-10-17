#include <gtest/gtest.h>

#include "SyntheticDisc.h"
#include "TestUtils.h"
#include "input/gc/RelModule.h"

using namespace GC;
using namespace TestUtils;

namespace {

using Synthetic::BuildRel;
using Synthetic::RelSpec;

RelRelocation R(uint16_t offset, uint8_t type, uint8_t section, uint32_t addend) {
    return Synthetic::Rel(offset, type, section, addend);
}

RelSpec TestSpec() {
    RelSpec spec;
    spec.id = 3;

    std::vector<uint8_t> text(0x40, 0);
    PutBE32(text, 0x00, 0x48000001);
    PutBE32(text, 0x04, 0x40820000);
    PutBE32(text, 0x08, 0x3C600000);
    PutBE32(text, 0x0C, 0x38630000);
    PutBE32(text, 0x10, 0x3C800000);
    PutBE32(text, 0x14, 0x48000002);
    PutBE32(text, 0x18, 0x40000000);

    std::vector<uint8_t> data = Pattern(0x40, 4);
    for (int i = 0; i < 0x2C; i++) {
        data[i] = 0;
    }

    spec.sections = {
        { false, {}, 0 },
        { true, text, 0 },
        { false, { 0, 0, 0, 0 }, 0 },
        { false, { 0, 0, 0, 0 }, 0 },
        { false, Pattern(0x60, 3), 0 },
        { false, data, 0 },
        { false, {}, 0x20 },
    };

    spec.imports = {
        { 3,
          {
              R(0, R_DOLPHIN_SECTION, 5, 0),
              R(0x00, R_PPC_ADDR32, 4, 0x10),
              R(0x04, R_PPC_ADDR32, 6, 0x08),
              R(0x10, R_DOLPHIN_NOP, 0, 0),
              R(0x04, R_PPC_ADDR32, 5, 0x30),
              R(0x04, R_PPC_ADDR16, 4, 0x1234),
              R(0x04, R_PPC_ADDR32, 2, 0),
              R(0, R_DOLPHIN_SECTION, 1, 0),
              R(0x00, R_PPC_REL24, 1, 0x20),
              R(0x04, R_PPC_REL14, 1, 0x00),
              R(0x06, R_PPC_ADDR16_HA, 5, 0x8000),
              R(0x04, R_PPC_ADDR16_LO, 5, 0x8000),
              R(0x04, R_PPC_ADDR16_HI, 5, 0x8000),
              R(0x02, R_PPC_ADDR24, 4, 0x40),
              R(0x04, R_PPC_ADDR14, 6, 0x10),
              R(0, R_DOLPHIN_SECTION, 2, 0),
              R(0x00, R_PPC_ADDR32, 5, 0),
              R(0, R_DOLPHIN_END, 0, 0),
          } },
        { 0,
          {
              R(0, R_DOLPHIN_SECTION, 5, 0),
              R(0x20, R_PPC_ADDR32, 0, 0x80123456),
              R(0x06, R_PPC_ADDR16_HA, 0, 0x8000ABCD),
              R(0, R_DOLPHIN_END, 0, 0),
          } },
        { 7,
          {
              R(0, R_DOLPHIN_SECTION, 5, 0),
              R(0x28, R_PPC_ADDR32, 1, 0),
              R(0, R_DOLPHIN_END, 0, 0),
          } },
    };
    return spec;
}

std::vector<std::optional<uint32_t>> TestBases() {
    return { std::nullopt, 0x82400000, std::nullopt, std::nullopt, 0x82000000, 0x81000000, 0x82100000 };
}

} // namespace

TEST(RelModule, ParsesHeaderSectionsAndImports) {
    RelModule rel(BuildRel(TestSpec()));
    EXPECT_EQ(rel.GetId(), 3u);
    EXPECT_EQ(rel.GetVersion(), 2u);
    EXPECT_EQ(rel.GetBssSize(), 0x20u);
    ASSERT_EQ(rel.GetSections().size(), 7u);
    EXPECT_TRUE(rel.GetSections()[1].exec);
    EXPECT_TRUE(rel.GetSections()[6].IsBss());
    ASSERT_EQ(rel.GetImports().size(), 3u);
    EXPECT_EQ(rel.GetImports()[1].moduleId, 0u);

    const auto names = rel.GetSectionNames();
    EXPECT_EQ(names[1], ".text");
    EXPECT_EQ(names[4], ".rodata");
    EXPECT_EQ(names[5], ".data");
    EXPECT_EQ(names[6], ".bss");
    EXPECT_EQ(rel.FindSection(".data"), 5u);
    EXPECT_EQ(rel.FindSection("4"), 4u);
    EXPECT_FALSE(rel.FindSection(".sdata").has_value());

    const auto data = rel.GetSectionData();
    EXPECT_EQ(data[6], std::vector<uint8_t>(0x20, 0));
}

TEST(RelModule, LinksLikeOSLink) {
    const auto spec = TestSpec();
    RelModule rel(BuildRel(spec));
    auto sections = rel.GetSectionData();
    const auto stats = rel.Link(TestBases(), sections);

    const auto& data = sections[5];
    EXPECT_EQ(GetBE32(data, 0x00), 0x82000010u);
    EXPECT_EQ(GetBE32(data, 0x04), 0x82100008u);
    EXPECT_EQ(GetBE32(data, 0x18), 0x81000030u);
    EXPECT_EQ(GetBE16(data, 0x1C), 0x1234u);
    EXPECT_EQ(GetBE32(data, 0x20), 0x80123456u);
    EXPECT_EQ(GetBE16(data, 0x26), 0x8001u);
    EXPECT_EQ(GetBE32(data, 0x20 + 0x8), 0u) << "relocations against other modules are not applied";
    for (size_t i = 0x2C; i < data.size(); i++) {
        EXPECT_EQ(data[i], spec.sections[5].data[i]);
    }

    const auto& text = sections[1];
    EXPECT_EQ(GetBE32(text, 0x00), 0x48000021u);
    EXPECT_EQ(GetBE32(text, 0x04), 0x4082FFFCu);
    EXPECT_EQ(GetBE16(text, 0x0A), 0x8101u);
    EXPECT_EQ(GetBE16(text, 0x0E), 0x8000u);
    EXPECT_EQ(GetBE16(text, 0x12), 0x8100u);
    EXPECT_EQ(GetBE32(text, 0x14), 0x4A000042u);
    EXPECT_EQ(GetBE32(text, 0x18), 0x40000010u);

    EXPECT_EQ(sections[2], spec.sections[2].data) << "unplaced sections are not patched";

    EXPECT_EQ(stats.applied, 13u);
    EXPECT_EQ(stats.unresolvedTarget, 1u);
    EXPECT_EQ(stats.skippedUnplaced, 1u);
    EXPECT_EQ(stats.unknownModule, 1u);
    EXPECT_EQ(stats.unknownType, 0u);
    EXPECT_EQ(stats.byType.at(R_PPC_ADDR32), 4u);
    EXPECT_EQ(stats.byType.at(R_PPC_ADDR16_HA), 2u);
}

TEST(RelModule, DecompressesYaz0) {
    const auto raw = BuildRel(TestSpec());
    EXPECT_EQ(RelModule::Decompress(Yaz0Literal(raw)), raw);
    EXPECT_EQ(RelModule::Decompress(raw), raw);
}

TEST(RelModule, RejectsTruncatedInput) {
    auto raw = BuildRel(TestSpec());
    raw.resize(0x30);
    EXPECT_THROW(RelModule rel(raw), std::runtime_error);
}
