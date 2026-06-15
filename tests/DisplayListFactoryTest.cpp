#include <gtest/gtest.h>
#include "factories/DisplayListFactory.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
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

// Parse GBI command pairs from a big-endian binary buffer
TEST(DisplayListFactoryTest, ManualGBICommandParse) {
    // Minimal display list: gsSPVertex + gsSPEndDisplayList
    // Each command is a pair of 32-bit words (w0, w1)
    std::vector<uint8_t> buf = {
        // gsSPVertex: w0=0x01020030 (opcode=0x01, n=2, v0+n=0, size=0x30)
        0x01, 0x02, 0x00, 0x30,
        // w1=0x06001000 (segment address of vertex data)
        0x06, 0x00, 0x10, 0x00,
        // gsSPEndDisplayList: w0=0xDF000000, w1=0x00000000
        0xDF, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<uint32_t> gfxs;
    // Read 2 commands (4 words)
    for (int i = 0; i < 4; i++) {
        gfxs.push_back(reader.ReadUInt32());
    }

    ASSERT_EQ(gfxs.size(), 4u);
    // First command: gsSPVertex
    EXPECT_EQ((gfxs[0] >> 24) & 0xFF, 0x01u);  // opcode
    EXPECT_EQ(gfxs[1], 0x06001000u);            // vertex address
    // Second command: gsSPEndDisplayList
    EXPECT_EQ((gfxs[2] >> 24) & 0xFF, 0xDFu);  // opcode
    EXPECT_EQ(gfxs[3], 0x00000000u);
}
