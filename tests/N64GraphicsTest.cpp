#include <gtest/gtest.h>

extern "C" {
#include "n64graphics/n64graphics.h"
}

#include <cstdlib>
#include <vector>

// RGBA16: 5-5-5-1 format, 2 bytes per pixel (big-endian)
// Bits: RRRRR GGGGG BBBBB A
TEST(N64GraphicsTest, Raw2RGBA16SinglePixel) {
    // White with alpha: R=31, G=31, B=31, A=1
    // Binary: 11111 11111 11111 1 = 0xFFFF
    uint8_t raw[] = {0xFF, 0xFF};
    rgba* img = raw2rgba(raw, 1, 1, 16);
    ASSERT_NE(img, nullptr);
    // SCALE_5_8(31) = (31 * 255) / 31 = 255
    EXPECT_EQ(img[0].red, 255);
    EXPECT_EQ(img[0].green, 255);
    EXPECT_EQ(img[0].blue, 255);
    EXPECT_EQ(img[0].alpha, 255);
    free(img);
}

TEST(N64GraphicsTest, Raw2RGBA16Black) {
    // Black with no alpha: R=0, G=0, B=0, A=0
    uint8_t raw[] = {0x00, 0x00};
    rgba* img = raw2rgba(raw, 1, 1, 16);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img[0].red, 0);
    EXPECT_EQ(img[0].green, 0);
    EXPECT_EQ(img[0].blue, 0);
    EXPECT_EQ(img[0].alpha, 0);
    free(img);
}

TEST(N64GraphicsTest, Raw2RGBA32SinglePixel) {
    // RGBA32: 8-8-8-8, 4 bytes per pixel — direct passthrough
    uint8_t raw[] = {0xAA, 0xBB, 0xCC, 0xDD};
    rgba* img = raw2rgba(raw, 1, 1, 32);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img[0].red, 0xAA);
    EXPECT_EQ(img[0].green, 0xBB);
    EXPECT_EQ(img[0].blue, 0xCC);
    EXPECT_EQ(img[0].alpha, 0xDD);
    free(img);
}

TEST(N64GraphicsTest, Raw2RGBA32MultiplePixels) {
    uint8_t raw[] = {
        255, 0, 0, 255,    // red
        0, 255, 0, 255,    // green
        0, 0, 255, 255,    // blue
        0, 0, 0, 0,        // transparent black
    };
    rgba* img = raw2rgba(raw, 2, 2, 32);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img[0].red, 255);
    EXPECT_EQ(img[1].green, 255);
    EXPECT_EQ(img[2].blue, 255);
    EXPECT_EQ(img[3].alpha, 0);
    free(img);
}

TEST(N64GraphicsTest, RGBA2RawRGBA32) {
    rgba img[] = {{0xAA, 0xBB, 0xCC, 0xDD}};
    uint8_t raw[4];
    int result = rgba2raw(raw, img, 1, 1, 32);
    EXPECT_EQ(result, 4);
    EXPECT_EQ(raw[0], 0xAA);
    EXPECT_EQ(raw[1], 0xBB);
    EXPECT_EQ(raw[2], 0xCC);
    EXPECT_EQ(raw[3], 0xDD);
}

TEST(N64GraphicsTest, RGBA32RoundTrip) {
    uint8_t original[] = {100, 150, 200, 255};
    rgba* img = raw2rgba(original, 1, 1, 32);
    ASSERT_NE(img, nullptr);

    uint8_t output[4];
    rgba2raw(output, img, 1, 1, 32);
    EXPECT_EQ(output[0], 100);
    EXPECT_EQ(output[1], 150);
    EXPECT_EQ(output[2], 200);
    EXPECT_EQ(output[3], 255);
    free(img);
}

TEST(N64GraphicsTest, RGBA2RawRGBA16) {
    // Pure white with alpha
    rgba img[] = {{255, 255, 255, 255}};
    uint8_t raw[2];
    int result = rgba2raw(raw, img, 1, 1, 16);
    EXPECT_EQ(result, 2);
    EXPECT_EQ(raw[0], 0xFF);
    EXPECT_EQ(raw[1], 0xFF);
}

// IA format tests

TEST(N64GraphicsTest, Raw2IA16SinglePixel) {
    // IA16: 8-bit intensity, 8-bit alpha — 2 bytes per pixel
    uint8_t raw[] = {0x80, 0xFF};
    ia* img = raw2ia(raw, 1, 1, 16);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img[0].intensity, 0x80);
    EXPECT_EQ(img[0].alpha, 0xFF);
    free(img);
}

TEST(N64GraphicsTest, Raw2IA8SinglePixel) {
    // IA8: 4-bit intensity, 4-bit alpha — 1 byte per pixel
    // 0xF0 = intensity nibble 0xF, alpha nibble 0x0
    // SCALE_4_8(0xF) = 0xF * 0x11 = 0xFF
    // SCALE_4_8(0x0) = 0
    uint8_t raw[] = {0xF0};
    ia* img = raw2ia(raw, 1, 1, 8);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img[0].intensity, 0xFF);
    EXPECT_EQ(img[0].alpha, 0x00);
    free(img);
}

TEST(N64GraphicsTest, Raw2IA8FullAlpha) {
    // 0xFF = intensity 0xF, alpha 0xF
    uint8_t raw[] = {0xFF};
    ia* img = raw2ia(raw, 1, 1, 8);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img[0].intensity, 0xFF);
    EXPECT_EQ(img[0].alpha, 0xFF);
    free(img);
}

TEST(N64GraphicsTest, IA16RoundTrip) {
    uint8_t original[] = {0x80, 0xC0};
    ia* img = raw2ia(original, 1, 1, 16);
    ASSERT_NE(img, nullptr);

    uint8_t output[2];
    ia2raw(output, img, 1, 1, 16);
    EXPECT_EQ(output[0], 0x80);
    EXPECT_EQ(output[1], 0xC0);
    free(img);
}

// I (intensity-only) format tests

TEST(N64GraphicsTest, Raw2I8SinglePixel) {
    // I8: 8-bit intensity, alpha is always 0xFF
    uint8_t raw[] = {0x80};
    ia* img = raw2i(raw, 1, 1, 8);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img[0].intensity, 0x80);
    EXPECT_EQ(img[0].alpha, 0xFF);
    free(img);
}

TEST(N64GraphicsTest, Raw2I4TwoPixels) {
    // I4: 4-bit intensity packed, 2 pixels per byte
    // 0xF0 = pixel0=0xF, pixel1=0x0
    // SCALE_4_8(0xF) = 0xFF, SCALE_4_8(0x0) = 0x00
    uint8_t raw[] = {0xF0};
    ia* img = raw2i(raw, 2, 1, 4);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img[0].intensity, 0xFF);
    EXPECT_EQ(img[0].alpha, 0xFF);
    EXPECT_EQ(img[1].intensity, 0x00);
    EXPECT_EQ(img[1].alpha, 0xFF);
    free(img);
}

TEST(N64GraphicsTest, I8RoundTrip) {
    uint8_t original[] = {0x40};
    ia* img = raw2i(original, 1, 1, 8);
    ASSERT_NE(img, nullptr);

    uint8_t output[1];
    i2raw(output, img, 1, 1, 8);
    EXPECT_EQ(output[0], 0x40);
    free(img);
}
