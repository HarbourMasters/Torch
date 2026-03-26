#include <gtest/gtest.h>
#include "factories/DisplayListFactory.h"
#include <vector>

TEST(DisplayListFactoryTest, DListDataDefaultConstruction) {
    DListData data;
    EXPECT_TRUE(data.mGfxs.empty());
}

TEST(DisplayListFactoryTest, DListDataConstruction) {
    std::vector<uint32_t> gfxs = {0xDE010000, 0x06001000, 0xDF000000, 0x00000000};
    DListData data(gfxs);
    ASSERT_EQ(data.mGfxs.size(), 4u);
    EXPECT_EQ(data.mGfxs[0], 0xDE010000u);
    EXPECT_EQ(data.mGfxs[1], 0x06001000u);
    EXPECT_EQ(data.mGfxs[2], 0xDF000000u);
    EXPECT_EQ(data.mGfxs[3], 0x00000000u);
}

TEST(DisplayListFactoryTest, Alignment) {
    DListFactory factory;
    EXPECT_EQ(factory.GetAlignment(), 8u);
}

TEST(DisplayListFactoryTest, HasExpectedExporters) {
    DListFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}

// GBI opcode extraction: the high byte of word 0 is the opcode
TEST(DisplayListFactoryTest, GBIOpcodeExtraction) {
    // gsSPEndDisplayList: w0 = 0xDF000000
    uint32_t endDL = 0xDF000000;
    EXPECT_EQ((endDL >> 24) & 0xFF, 0xDFu);

    // gsSPDisplayList (branch): w0 = 0xDE010000
    uint32_t branchDL = 0xDE010000;
    EXPECT_EQ((branchDL >> 24) & 0xFF, 0xDEu);

    // gsSPVertex: w0 = 0x01012010 (F3DEX2 opcode 0x01)
    uint32_t spVertex = 0x01012010;
    EXPECT_EQ((spVertex >> 24) & 0xFF, 0x01u);
}
