#include <gtest/gtest.h>
#include "factories/FloatFactory.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include <vector>
#include <cmath>

TEST(FloatFactoryTest, FloatDataConstruction) {
    std::vector<float> floats = {1.0f, -2.5f, 3.14159f, 0.0f};
    FloatData data(floats);
    ASSERT_EQ(data.mFloats.size(), 4u);
    EXPECT_FLOAT_EQ(data.mFloats[0], 1.0f);
    EXPECT_FLOAT_EQ(data.mFloats[1], -2.5f);
    EXPECT_FLOAT_EQ(data.mFloats[2], 3.14159f);
    EXPECT_FLOAT_EQ(data.mFloats[3], 0.0f);
}

TEST(FloatFactoryTest, FloatDataEmpty) {
    std::vector<float> floats;
    FloatData data(floats);
    EXPECT_TRUE(data.mFloats.empty());
}

TEST(FloatFactoryTest, HasExpectedExporters) {
    FloatFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Code));
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}

TEST(FloatFactoryTest, ParseModdingReturnsNullopt) {
    FloatFactory factory;
    std::vector<uint8_t> buf;
    YAML::Node node;
    EXPECT_EQ(factory.parse_modding(buf, node), std::nullopt);
}

TEST(FloatFactoryTest, ManualFloatParse) {
    // Big-endian IEEE 754 floats:
    // 1.0f = 0x3F800000
    // -2.5f = 0xC0200000
    // 0.0f = 0x00000000
    std::vector<uint8_t> buf = {
        0x3F, 0x80, 0x00, 0x00,  // 1.0f
        0xC0, 0x20, 0x00, 0x00,  // -2.5f
        0x00, 0x00, 0x00, 0x00,  // 0.0f
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<float> floats;
    for (int i = 0; i < 3; i++) {
        floats.push_back(reader.ReadFloat());
    }

    ASSERT_EQ(floats.size(), 3u);
    EXPECT_FLOAT_EQ(floats[0], 1.0f);
    EXPECT_FLOAT_EQ(floats[1], -2.5f);
    EXPECT_FLOAT_EQ(floats[2], 0.0f);
}
