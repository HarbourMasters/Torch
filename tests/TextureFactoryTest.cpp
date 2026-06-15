#include <gtest/gtest.h>
#include "factories/TextureFactory.h"
#include <vector>

TEST(TextureFactoryTest, TextureFormatConstruction) {
    TextureFormat fmt = {TextureType::RGBA16bpp, 16};
    EXPECT_EQ(fmt.type, TextureType::RGBA16bpp);
    EXPECT_EQ(fmt.depth, 16u);
}

TEST(TextureFactoryTest, TextureFormatDepthValues) {
    // Verify expected depth for each format type
    TextureFormat rgba32 = {TextureType::RGBA32bpp, 32};
    TextureFormat rgba16 = {TextureType::RGBA16bpp, 16};
    TextureFormat ci4 = {TextureType::Palette4bpp, 4};
    TextureFormat ci8 = {TextureType::Palette8bpp, 8};
    TextureFormat i4 = {TextureType::Grayscale4bpp, 4};
    TextureFormat i8 = {TextureType::Grayscale8bpp, 8};
    TextureFormat ia1 = {TextureType::GrayscaleAlpha1bpp, 1};
    TextureFormat ia4 = {TextureType::GrayscaleAlpha4bpp, 4};
    TextureFormat ia8 = {TextureType::GrayscaleAlpha8bpp, 8};
    TextureFormat ia16 = {TextureType::GrayscaleAlpha16bpp, 16};
    TextureFormat tlut = {TextureType::TLUT, 16};

    EXPECT_EQ(rgba32.depth, 32u);
    EXPECT_EQ(rgba16.depth, 16u);
    EXPECT_EQ(ci4.depth, 4u);
    EXPECT_EQ(ci8.depth, 8u);
    EXPECT_EQ(i4.depth, 4u);
    EXPECT_EQ(i8.depth, 8u);
    EXPECT_EQ(ia1.depth, 1u);
    EXPECT_EQ(ia4.depth, 4u);
    EXPECT_EQ(ia8.depth, 8u);
    EXPECT_EQ(ia16.depth, 16u);
    EXPECT_EQ(tlut.depth, 16u);
}

TEST(TextureFactoryTest, TextureDataConstruction) {
    TextureFormat fmt = {TextureType::RGBA32bpp, 32};
    std::vector<uint8_t> buffer = {0xFF, 0x00, 0x00, 0xFF};  // single red pixel
    TextureData data(fmt, 1, 1, buffer);
    EXPECT_EQ(data.mFormat.type, TextureType::RGBA32bpp);
    EXPECT_EQ(data.mFormat.depth, 32u);
    EXPECT_EQ(data.mWidth, 1u);
    EXPECT_EQ(data.mHeight, 1u);
    ASSERT_EQ(data.mBuffer.size(), 4u);
    EXPECT_EQ(data.mBuffer[0], 0xFF);
}

TEST(TextureFactoryTest, TextureDataLargerBuffer) {
    TextureFormat fmt = {TextureType::RGBA16bpp, 16};
    // 4x4 RGBA16 = 32 bytes
    std::vector<uint8_t> buffer(32, 0xAA);
    TextureData data(fmt, 4, 4, buffer);
    EXPECT_EQ(data.mWidth, 4u);
    EXPECT_EQ(data.mHeight, 4u);
    EXPECT_EQ(data.mBuffer.size(), 32u);
}

TEST(TextureFactoryTest, TextureFactoryExporters) {
    TextureFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Modding).has_value());
}

TEST(TextureFactoryTest, TextureFactorySupportsModding) {
    TextureFactory factory;
    EXPECT_TRUE(factory.SupportModdedAssets());
}
