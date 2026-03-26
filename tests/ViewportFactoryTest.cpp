#include <gtest/gtest.h>
#include "factories/ViewportFactory.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include <vector>

TEST(ViewportFactoryTest, VpRawSize) {
    EXPECT_EQ(sizeof(VpRaw), 16u);
}

TEST(ViewportFactoryTest, VpDataConstruction) {
    VpRaw vp = {{160, 120, 511, 0}, {160, 120, 511, 0}};
    VpData data(vp);
    EXPECT_EQ(data.mViewport.vscale[0], 160);
    EXPECT_EQ(data.mViewport.vscale[1], 120);
    EXPECT_EQ(data.mViewport.vscale[2], 511);
    EXPECT_EQ(data.mViewport.vscale[3], 0);
    EXPECT_EQ(data.mViewport.vtrans[0], 160);
    EXPECT_EQ(data.mViewport.vtrans[1], 120);
    EXPECT_EQ(data.mViewport.vtrans[2], 511);
    EXPECT_EQ(data.mViewport.vtrans[3], 0);
}

TEST(ViewportFactoryTest, HasExpectedExporters) {
    ViewportFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Code));
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}

TEST(ViewportFactoryTest, ManualViewportParse) {
    // Build a 16-byte big-endian viewport buffer:
    // vscale: (640, 480, 511, 0) → 0x0280, 0x01E0, 0x01FF, 0x0000
    // vtrans: (640, 480, 511, 0) → 0x0280, 0x01E0, 0x01FF, 0x0000
    std::vector<uint8_t> buf = {
        0x02, 0x80,  // vscale[0] = 640
        0x01, 0xE0,  // vscale[1] = 480
        0x01, 0xFF,  // vscale[2] = 511
        0x00, 0x00,  // vscale[3] = 0
        0x02, 0x80,  // vtrans[0] = 640
        0x01, 0xE0,  // vtrans[1] = 480
        0x01, 0xFF,  // vtrans[2] = 511
        0x00, 0x00,  // vtrans[3] = 0
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    VpRaw vp;
    for (int i = 0; i < 4; i++) {
        vp.vscale[i] = reader.ReadInt16();
    }
    for (int i = 0; i < 4; i++) {
        vp.vtrans[i] = reader.ReadInt16();
    }

    EXPECT_EQ(vp.vscale[0], 640);
    EXPECT_EQ(vp.vscale[1], 480);
    EXPECT_EQ(vp.vscale[2], 511);
    EXPECT_EQ(vp.vscale[3], 0);
    EXPECT_EQ(vp.vtrans[0], 640);
    EXPECT_EQ(vp.vtrans[1], 480);
    EXPECT_EQ(vp.vtrans[2], 511);
    EXPECT_EQ(vp.vtrans[3], 0);
}

TEST(ViewportFactoryTest, NegativeValues) {
    // Test with negative scale/translation values
    std::vector<uint8_t> buf = {
        0xFF, 0x00,  // vscale[0] = -256
        0x80, 0x00,  // vscale[1] = -32768
        0x00, 0x01,  // vscale[2] = 1
        0x00, 0x00,  // vscale[3] = 0
        0x00, 0x00,  // vtrans[0] = 0
        0x00, 0x00,  // vtrans[1] = 0
        0x00, 0x00,  // vtrans[2] = 0
        0x00, 0x00,  // vtrans[3] = 0
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    VpRaw vp;
    for (int i = 0; i < 4; i++) {
        vp.vscale[i] = reader.ReadInt16();
    }
    for (int i = 0; i < 4; i++) {
        vp.vtrans[i] = reader.ReadInt16();
    }

    EXPECT_EQ(vp.vscale[0], -256);
    EXPECT_EQ(vp.vscale[1], -32768);
    EXPECT_EQ(vp.vscale[2], 1);
}
