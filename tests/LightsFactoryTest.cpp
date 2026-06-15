#include <gtest/gtest.h>
#include "factories/LightsFactory.h"
#include <cstring>

TEST(LightsFactoryTest, StructSizes) {
    EXPECT_EQ(sizeof(AmbientRaw), 8u);
    EXPECT_EQ(sizeof(LightRaw), 16u);
    EXPECT_EQ(sizeof(Lights1Raw), 24u);
}

TEST(LightsFactoryTest, LightsDataConstruction) {
    Lights1Raw raw = {};
    // Ambient: bright white
    raw.a.l.col[0] = 64; raw.a.l.col[1] = 64; raw.a.l.col[2] = 64;
    raw.a.l.colc[0] = 64; raw.a.l.colc[1] = 64; raw.a.l.colc[2] = 64;
    // Diffuse: red light pointing down
    raw.l[0].l.col[0] = 255; raw.l[0].l.col[1] = 0; raw.l[0].l.col[2] = 0;
    raw.l[0].l.colc[0] = 255; raw.l[0].l.colc[1] = 0; raw.l[0].l.colc[2] = 0;
    raw.l[0].l.dir[0] = 0; raw.l[0].l.dir[1] = 127; raw.l[0].l.dir[2] = 0;

    LightsData data(raw);
    EXPECT_EQ(data.mLights.a.l.col[0], 64);
    EXPECT_EQ(data.mLights.a.l.col[1], 64);
    EXPECT_EQ(data.mLights.a.l.col[2], 64);
    EXPECT_EQ(data.mLights.l[0].l.col[0], 255);
    EXPECT_EQ(data.mLights.l[0].l.col[1], 0);
    EXPECT_EQ(data.mLights.l[0].l.col[2], 0);
    EXPECT_EQ(data.mLights.l[0].l.dir[0], 0);
    EXPECT_EQ(data.mLights.l[0].l.dir[1], 127);
    EXPECT_EQ(data.mLights.l[0].l.dir[2], 0);
}

TEST(LightsFactoryTest, HasExpectedExporters) {
    LightsFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Code));
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}

TEST(LightsFactoryTest, ManualLightsParse) {
    // Build a 24-byte buffer representing Lights1Raw:
    // Ambient (8 bytes): col={100,150,200}, pad1=0, colc={100,150,200}, pad2=0
    // Light (16 bytes): col={255,128,64}, pad1=0, colc={255,128,64}, pad2=0, dir={40,-50,73}, pad3=0
    uint8_t buf[24] = {
        // AmbientRaw (8 bytes)
        100, 150, 200, 0,       // col[3] + pad1
        100, 150, 200, 0,       // colc[3] + pad2
        // LightRaw (16 bytes)
        255, 128, 64, 0,        // col[3] + pad1
        255, 128, 64, 0,        // colc[3] + pad2
        40, static_cast<uint8_t>(-50), 73, 0,  // dir[3] + pad3
        0, 0, 0, 0,             // padding for alignment
    };

    Lights1Raw lights;
    std::memcpy(&lights, buf, sizeof(Lights1Raw));

    EXPECT_EQ(lights.a.l.col[0], 100);
    EXPECT_EQ(lights.a.l.col[1], 150);
    EXPECT_EQ(lights.a.l.col[2], 200);
    EXPECT_EQ(lights.l[0].l.col[0], 255);
    EXPECT_EQ(lights.l[0].l.col[1], 128);
    EXPECT_EQ(lights.l[0].l.col[2], 64);
    EXPECT_EQ(lights.l[0].l.dir[0], 40);
    EXPECT_EQ(lights.l[0].l.dir[1], -50);
    EXPECT_EQ(lights.l[0].l.dir[2], 73);
}
