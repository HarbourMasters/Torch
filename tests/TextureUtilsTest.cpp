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
    // Returns decoded/output buffer size (1 byte per pixel), not raw/encoded size.
    // alloc_ia8_text_from_i1 expands 1-bit input to 8-bit output, so the texture
    // in memory is width*height bytes. See TODO in TextureUtils.cpp.
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

// alloc_ia8_text_from_i1 tests
// Converts 1-bit-per-pixel data (packed in big-endian uint16) to 8-bit-per-pixel

TEST(TextureUtilsTest, AllocIA8AllOnes) {
    // All bits set: 0xFFFF in big-endian → all 16 output bytes should be 0xFF
    // BSWAP16 will swap the bytes, so we need to store in native endian
    // such that after BSWAP16 we get 0xFFFF (which is still 0xFFFF)
    uint16_t input = 0xFFFF;
    auto result = TextureUtils::alloc_ia8_text_from_i1(&input, 16, 1);
    ASSERT_EQ(result.size(), 16u);
    for (size_t i = 0; i < 16; i++) {
        EXPECT_EQ(result[i], 0xFF) << "byte " << i;
    }
}

TEST(TextureUtilsTest, AllocIA8AllZeros) {
    uint16_t input = 0x0000;
    auto result = TextureUtils::alloc_ia8_text_from_i1(&input, 16, 1);
    ASSERT_EQ(result.size(), 16u);
    for (size_t i = 0; i < 16; i++) {
        EXPECT_EQ(result[i], 0x00) << "byte " << i;
    }
}

TEST(TextureUtilsTest, AllocIA8NonPalindrome) {
    // 0x8000 is NOT a byte-palindrome, so BSWAP16 changes it to 0x0080.
    // Bit pattern of 0x0080: only bit 7 is set.
    // bitMask iterates from 0x8000 down, so bit 7 is hit at output index 8.
    uint16_t input = 0x8000;
    auto result = TextureUtils::alloc_ia8_text_from_i1(&input, 16, 1);
    ASSERT_EQ(result.size(), 16u);
    for (size_t i = 0; i < 16; i++) {
        if (i == 8) {
            EXPECT_EQ(result[i], 0xFF) << "byte " << i << " should be set";
        } else {
            EXPECT_EQ(result[i], 0x00) << "byte " << i << " should be clear";
        }
    }
}

TEST(TextureUtilsTest, AllocIA8OutputSize) {
    // 32x16 = 512 pixels, input needs 512/16 = 32 uint16_t values
    std::vector<uint16_t> input(32, 0);
    auto result = TextureUtils::alloc_ia8_text_from_i1(input.data(), 32, 16);
    EXPECT_EQ(result.size(), 512u);
}
