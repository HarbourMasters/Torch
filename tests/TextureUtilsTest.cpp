#include <gtest/gtest.h>
#include "utils/TextureUtils.h"

// CalculateTextureSize for 32x32 textures

TEST(TextureUtilsTest, RGBA32bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::RGBA32bpp, 32, 32), 4096u);
}

TEST(TextureUtilsTest, RGBA16bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::RGBA16bpp, 32, 32), 2048u);
}

TEST(TextureUtilsTest, GrayscaleAlpha16bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::GrayscaleAlpha16bpp, 32, 32), 2048u);
}

TEST(TextureUtilsTest, TLUT) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::TLUT, 32, 32), 2048u);
}

TEST(TextureUtilsTest, Grayscale8bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::Grayscale8bpp, 32, 32), 1024u);
}

TEST(TextureUtilsTest, Palette8bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::Palette8bpp, 32, 32), 1024u);
}

TEST(TextureUtilsTest, GrayscaleAlpha8bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::GrayscaleAlpha8bpp, 32, 32), 1024u);
}

TEST(TextureUtilsTest, GrayscaleAlpha1bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::GrayscaleAlpha1bpp, 32, 32), 1024u);
}

TEST(TextureUtilsTest, Palette4bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::Palette4bpp, 32, 32), 512u);
}

TEST(TextureUtilsTest, Grayscale4bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::Grayscale4bpp, 32, 32), 512u);
}

TEST(TextureUtilsTest, GrayscaleAlpha4bpp) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::GrayscaleAlpha4bpp, 32, 32), 512u);
}

TEST(TextureUtilsTest, ErrorTypeReturnsZero) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::Error, 32, 32), 0u);
}

// Non-square and edge cases

TEST(TextureUtilsTest, NonSquareTexture) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::RGBA32bpp, 64, 16), 64u * 16u * 4u);
}

TEST(TextureUtilsTest, OneByOneTexture) {
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::RGBA32bpp, 1, 1), 4u);
}

TEST(TextureUtilsTest, Palette4bppOddDimension) {
    // 16x16 / 2 = 128
    EXPECT_EQ(TextureUtils::CalculateTextureSize(TextureType::Palette4bpp, 16, 16), 128u);
}
