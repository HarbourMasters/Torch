#include <gtest/gtest.h>
#include "factories/VtxFactory.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include <vector>

TEST(VtxFactoryTest, VtxRawSize) {
    // Each N64 vertex is exactly 16 bytes
    EXPECT_EQ(sizeof(VtxRaw), 16u);
}

TEST(VtxFactoryTest, VtxDataConstruction) {
    VtxRaw v1 = {{1, 2, 3}, 0, {10, 20}, {255, 128, 64, 32}};
    VtxRaw v2 = {{4, 5, 6}, 0, {30, 40}, {0, 0, 0, 255}};
    std::vector<VtxRaw> vtxs = {v1, v2};

    VtxData data(vtxs);
    ASSERT_EQ(data.mVtxs.size(), 2u);
    EXPECT_EQ(data.mVtxs[0].ob[0], 1);
    EXPECT_EQ(data.mVtxs[0].ob[1], 2);
    EXPECT_EQ(data.mVtxs[0].ob[2], 3);
    EXPECT_EQ(data.mVtxs[0].cn[0], 255);
    EXPECT_EQ(data.mVtxs[1].ob[0], 4);
}

TEST(VtxFactoryTest, Alignment) {
    VtxFactory factory;
    EXPECT_EQ(factory.GetAlignment(), 8u);
}

TEST(VtxFactoryTest, HasExpectedExporters) {
    VtxFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Code));
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}

// Parse a vertex manually from a BinaryReader (same logic as VtxFactory::parse)
TEST(VtxFactoryTest, ManualVertexParse) {
    // Build a 16-byte big-endian vertex buffer:
    // ob[3]: (100, -50, 200) → 0x0064, 0xFFCE, 0x00C8
    // flag: 0
    // tc[2]: (512, 1024) → 0x0200, 0x0400
    // cn[4]: (255, 128, 64, 255)
    std::vector<uint8_t> buf = {
        0x00, 0x64,  // ob[0] = 100
        0xFF, 0xCE,  // ob[1] = -50
        0x00, 0xC8,  // ob[2] = 200
        0x00, 0x00,  // flag = 0
        0x02, 0x00,  // tc[0] = 512
        0x04, 0x00,  // tc[1] = 1024
        0xFF,        // cn[0] = 255
        0x80,        // cn[1] = 128
        0x40,        // cn[2] = 64
        0xFF,        // cn[3] = 255
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    VtxRaw vtx;
    vtx.ob[0] = reader.ReadInt16();
    vtx.ob[1] = reader.ReadInt16();
    vtx.ob[2] = reader.ReadInt16();
    vtx.flag = reader.ReadUInt16();
    vtx.tc[0] = reader.ReadInt16();
    vtx.tc[1] = reader.ReadInt16();
    vtx.cn[0] = reader.ReadUByte();
    vtx.cn[1] = reader.ReadUByte();
    vtx.cn[2] = reader.ReadUByte();
    vtx.cn[3] = reader.ReadUByte();

    EXPECT_EQ(vtx.ob[0], 100);
    EXPECT_EQ(vtx.ob[1], -50);
    EXPECT_EQ(vtx.ob[2], 200);
    EXPECT_EQ(vtx.flag, 0);
    EXPECT_EQ(vtx.tc[0], 512);
    EXPECT_EQ(vtx.tc[1], 1024);
    EXPECT_EQ(vtx.cn[0], 255);
    EXPECT_EQ(vtx.cn[1], 128);
    EXPECT_EQ(vtx.cn[2], 64);
    EXPECT_EQ(vtx.cn[3], 255);
}
