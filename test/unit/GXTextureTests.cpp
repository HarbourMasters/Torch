#include <gtest/gtest.h>

#include "TestUtils.h"
#include "gc/GXTexture.h"

#include <cstring>
#include <random>

using namespace GX;

namespace {

struct RefLayout {
    uint32_t bw;
    uint32_t bh;
    uint32_t bpp;
};

RefLayout RefLayoutFor(TexFormat format) {
    switch (format) {
        case TexFormat::I4:
        case TexFormat::C4:
            return { 8, 8, 4 };
        case TexFormat::I8:
        case TexFormat::IA4:
        case TexFormat::C8:
            return { 8, 4, 8 };
        default:
            return { 4, 4, 16 };
    }
}

size_t RefTiledBitOffset(uint32_t x, uint32_t y, uint32_t width, const RefLayout& l) {
    const uint32_t tilesPerRow = (width + l.bw - 1) / l.bw;
    const size_t tile = static_cast<size_t>(y / l.bh) * tilesPerRow + (x / l.bw);
    const size_t inner = static_cast<size_t>(y % l.bh) * l.bw + (x % l.bw);
    return (tile * l.bw * l.bh + inner) * l.bpp;
}

uint32_t RefReadBits(const std::vector<uint8_t>& buf, size_t bitOffset, uint32_t bpp) {
    const size_t byte = bitOffset / 8;
    if (bpp == 4) {
        return (bitOffset % 8) == 0 ? buf[byte] >> 4 : buf[byte] & 0xF;
    }
    if (bpp == 8) {
        return buf[byte];
    }
    return (buf[byte] << 8) | buf[byte + 1];
}

void RefWriteBits(std::vector<uint8_t>& buf, size_t bitOffset, uint32_t bpp, uint32_t value) {
    const size_t byte = bitOffset / 8;
    if (bpp == 4) {
        if ((bitOffset % 8) == 0) {
            buf[byte] = (buf[byte] & 0x0F) | (value << 4);
        } else {
            buf[byte] = (buf[byte] & 0xF0) | (value & 0xF);
        }
    } else if (bpp == 8) {
        buf[byte] = value;
    } else {
        buf[byte] = value >> 8;
        buf[byte + 1] = value;
    }
}

std::vector<uint8_t> RefTile(TexFormat format, const std::vector<uint8_t>& linear, uint32_t w, uint32_t h) {
    const auto l = RefLayoutFor(format);
    const uint32_t pw = (w + l.bw - 1) / l.bw * l.bw;
    const uint32_t ph = (h + l.bh - 1) / l.bh * l.bh;
    std::vector<uint8_t> tiled(static_cast<size_t>(pw) * ph * l.bpp / 8, 0xCD);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const uint32_t v = RefReadBits(linear, (static_cast<size_t>(y) * w + x) * l.bpp, l.bpp);
            RefWriteBits(tiled, RefTiledBitOffset(x, y, w, l), l.bpp, v);
        }
    }
    return tiled;
}

std::vector<uint8_t> RefDetile(TexFormat format, const std::vector<uint8_t>& tiled, uint32_t w, uint32_t h) {
    const auto l = RefLayoutFor(format);
    std::vector<uint8_t> linear((static_cast<size_t>(w) * h * l.bpp + 7) / 8, 0);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const uint32_t v = RefReadBits(tiled, RefTiledBitOffset(x, y, w, l), l.bpp);
            RefWriteBits(linear, (static_cast<size_t>(y) * w + x) * l.bpp, l.bpp, v);
        }
    }
    return linear;
}

} // namespace

TEST(GXTexture, TiledSizePadsToWholeTiles) {
    EXPECT_EQ(TiledSize(TexFormat::C4, 8, 8), 32u);
    EXPECT_EQ(TiledSize(TexFormat::C4, 12, 4), 64u);
    EXPECT_EQ(TiledSize(TexFormat::I8, 8, 5), 64u);
    EXPECT_EQ(TiledSize(TexFormat::RGB5A3, 4, 4), 32u);
    EXPECT_EQ(TiledSize(TexFormat::RGB5A3, 6, 2), 64u);
    EXPECT_EQ(TiledSize(TexFormat::RGBA8, 4, 4), 64u);
    EXPECT_EQ(TiledSize(TexFormat::CMPR, 8, 8), 32u);
}

TEST(GXTexture, DetileMatchesReferenceForEveryTexelFormat) {
    const TexFormat formats[] = { TexFormat::I4,     TexFormat::I8,     TexFormat::IA4, TexFormat::IA8, TexFormat::RGB565,
                                  TexFormat::RGB5A3, TexFormat::C4,     TexFormat::C8,  TexFormat::C14X2 };
    const std::pair<uint32_t, uint32_t> sizes[] = { { 8, 8 }, { 32, 16 }, { 16, 32 }, { 12, 6 }, { 64, 4 }, { 4, 8 } };

    uint32_t seed = 1;
    for (auto format : formats) {
        for (auto [w, h] : sizes) {
            const size_t tiledSize = TiledSize(format, w, h);
            const auto tiled = TestUtils::Pattern(tiledSize, seed++);
            EXPECT_EQ(Detile(format, tiled.data(), tiled.size(), w, h), RefDetile(format, tiled, w, h))
                << "format " << static_cast<int>(format) << " " << w << "x" << h;

            const auto linear = RefDetile(format, tiled, w, h);
            const auto retiled = RefTile(format, linear, w, h);
            EXPECT_EQ(Detile(format, retiled.data(), retiled.size(), w, h), linear);
        }
    }
}

TEST(GXTexture, DetileRgba8InterleavesArAndGbHalves) {
    const uint32_t w = 8, h = 4;
    const auto tiled = TestUtils::Pattern(TiledSize(TexFormat::RGBA8, w, h), 77);
    const auto linear = Detile(TexFormat::RGBA8, tiled.data(), tiled.size(), w, h);
    ASSERT_EQ(linear.size(), w * h * 4u);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const size_t block = ((y / 4) * ((w + 3) / 4) + x / 4) * 64;
            const size_t i = (y % 4) * 4 + (x % 4);
            const uint8_t* px = &linear[(y * w + x) * 4];
            EXPECT_EQ(px[0], tiled[block + i * 2 + 1]);
            EXPECT_EQ(px[1], tiled[block + 32 + i * 2]);
            EXPECT_EQ(px[2], tiled[block + 32 + i * 2 + 1]);
            EXPECT_EQ(px[3], tiled[block + i * 2]);
        }
    }
}

TEST(GXTexture, Rgb5a3DecodesBothForms) {
    uint8_t px[4];
    Rgb5a3ToRgba8(0x8000 | (31 << 10) | (16 << 5) | 1, px);
    EXPECT_EQ(px[0], 255);
    EXPECT_EQ(px[1], (16 << 3) | (16 >> 2));
    EXPECT_EQ(px[2], (1 << 3) | (1 >> 2));
    EXPECT_EQ(px[3], 255);

    Rgb5a3ToRgba8(0x3A5F, px);
    EXPECT_EQ(px[0], 0xA * 17);
    EXPECT_EQ(px[1], 0x5 * 17);
    EXPECT_EQ(px[2], 0xF * 17);
    EXPECT_EQ(px[3], (3 << 5) | (3 << 2) | (3 >> 1));
}

TEST(GXTexture, Rgb5a3ToRgba5551) {
    const uint16_t opaque = 0x8000 | (21 << 10) | (7 << 5) | 30;
    EXPECT_EQ(Rgb5a3ToRgba5551(opaque), (21 << 11) | (7 << 6) | (30 << 1) | 1);
    EXPECT_TRUE(Rgb5a3IsLossless(opaque));

    EXPECT_EQ(Rgb5a3ToRgba5551(0x7F0F), (31 << 11) | (0 << 6) | (31 << 1) | 1);
    EXPECT_TRUE(Rgb5a3IsLossless(0x7F0F));
    EXPECT_FALSE(Rgb5a3IsLossless(0x7F8F));

    EXPECT_EQ(Rgb5a3ToRgba5551(0x0000), 0);
    EXPECT_TRUE(Rgb5a3IsLossless(0x0ABC));
    EXPECT_EQ(Rgb5a3ToRgba5551(0x0ABC) & 1, 0);

    EXPECT_FALSE(Rgb5a3IsLossless(0x3FFF));
    EXPECT_TRUE(Rgb5a3HasPartialAlpha(0x3FFF));
    EXPECT_FALSE(Rgb5a3HasPartialAlpha(0x7FFF));
    EXPECT_FALSE(Rgb5a3HasPartialAlpha(0x0FFF));
    EXPECT_FALSE(Rgb5a3HasPartialAlpha(0xBFFF));
}

TEST(GXTexture, ConvertIndexedAndIntensityFormatsAreDetileOnly) {
    const struct {
        N64Format n64;
        TexFormat gx;
    } cases[] = { { N64Format::CI4, TexFormat::C4 },
                  { N64Format::CI8, TexFormat::C8 },
                  { N64Format::I4, TexFormat::I4 },
                  { N64Format::I8, TexFormat::I8 } };
    for (const auto& c : cases) {
        const auto tiled = TestUtils::Pattern(TiledSize(c.gx, 16, 8), 9);
        const auto out = ConvertToN64(c.n64, tiled.data(), tiled.size(), 16, 8);
        EXPECT_EQ(out.format, c.n64);
        EXPECT_EQ(out.data, RefDetile(c.gx, tiled, 16, 8));
    }
}

TEST(GXTexture, ConvertIa4ToN64Ia8SwapsNibbles) {
    std::vector<uint8_t> tiled(TiledSize(TexFormat::IA4, 8, 4), 0);
    tiled[0] = 0xA5;
    tiled[9] = 0x3C;
    const auto out = ConvertToN64(N64Format::IA8, tiled.data(), tiled.size(), 8, 4);
    EXPECT_EQ(out.format, N64Format::IA8);
    EXPECT_EQ(out.data[0], 0x5A);
    EXPECT_EQ(out.data[8 + 1], 0xC3);

    uint8_t px[4];
    const auto rgba = DecodeRGBA8(TexFormat::IA4, tiled.data(), tiled.size(), 8, 4);
    std::memcpy(px, rgba.data(), 4);
    EXPECT_EQ(px[0], 0x55);
    EXPECT_EQ(px[3], 0xAA);
}

TEST(GXTexture, ConvertIa8ToN64Ia16SwapsBytes) {
    std::vector<uint8_t> tiled(TiledSize(TexFormat::IA8, 4, 4), 0);
    tiled[0] = 0x12;
    tiled[1] = 0x34;
    const auto out = ConvertToN64(N64Format::IA16, tiled.data(), tiled.size(), 4, 4);
    EXPECT_EQ(out.format, N64Format::IA16);
    EXPECT_EQ(out.data[0], 0x34);
    EXPECT_EQ(out.data[1], 0x12);

    const auto rgba = DecodeRGBA8(TexFormat::IA8, tiled.data(), tiled.size(), 4, 4);
    EXPECT_EQ(rgba[0], 0x34);
    EXPECT_EQ(rgba[3], 0x12);
}

TEST(GXTexture, LosslessRgb5a3StaysSixteenBit) {
    std::vector<uint8_t> linear(8 * 4 * 2);
    for (size_t i = 0; i < 32; i++) {
        const uint16_t v = (i % 3 == 0) ? 0x0000 : (i % 3 == 1) ? (0x8000 | (uint16_t)(i * 997)) : 0x7F0F;
        TestUtils::PutBE16(linear, i * 2, v);
    }
    const auto tiled = RefTile(TexFormat::RGB5A3, linear, 8, 4);
    EXPECT_TRUE(Rgb5a3TextureIsLossless(tiled.data(), tiled.size(), 8, 4));

    const auto out = ConvertToN64(N64Format::RGBA16, tiled.data(), tiled.size(), 8, 4);
    ASSERT_EQ(out.format, N64Format::RGBA16);
    for (size_t i = 0; i < 32; i++) {
        EXPECT_EQ(TestUtils::GetBE16(out.data, i * 2), Rgb5a3ToRgba5551(TestUtils::GetBE16(linear, i * 2)));
    }
}

TEST(GXTexture, LossyRgb5a3IsPromotedToRgba32) {
    std::vector<uint8_t> linear(4 * 4 * 2, 0x80);
    TestUtils::PutBE16(linear, 10, 0x3ABC);
    const auto tiled = RefTile(TexFormat::RGB5A3, linear, 4, 4);
    EXPECT_FALSE(Rgb5a3TextureIsLossless(tiled.data(), tiled.size(), 4, 4));

    const auto out = ConvertToN64(N64Format::RGBA16, tiled.data(), tiled.size(), 4, 4);
    ASSERT_EQ(out.format, N64Format::RGBA32);
    ASSERT_EQ(out.data.size(), 4 * 4 * 4u);
    uint8_t expected[4];
    Rgb5a3ToRgba8(0x3ABC, expected);
    EXPECT_EQ(0, std::memcmp(&out.data[5 * 4], expected, 4));
}

TEST(GXTexture, Rgba8ConvertsToRgba32) {
    const auto tiled = TestUtils::Pattern(TiledSize(TexFormat::RGBA8, 4, 4), 5);
    const auto out = ConvertToN64(N64Format::RGBA32, tiled.data(), tiled.size(), 4, 4);
    EXPECT_EQ(out.format, N64Format::RGBA32);
    EXPECT_EQ(out.data[0], tiled[1]);
    EXPECT_EQ(out.data[3], tiled[0]);
}

TEST(GXTexture, TlutConversionCountsPartialAlpha) {
    std::vector<uint8_t> tlut;
    for (uint16_t v : { (uint16_t)(0x8000 | (3 << 10) | (4 << 5) | 5), (uint16_t)0x3FFF, (uint16_t)0x7000,
                        (uint16_t)0x0123, (uint16_t)0x6F0F }) {
        tlut.push_back(v >> 8);
        tlut.push_back(v & 0xFF);
    }
    const auto out = ConvertTlut(tlut.data(), 5);
    EXPECT_EQ(out.partialAlpha, 2u);
    EXPECT_EQ(TestUtils::GetBE16(out.data, 0), (3 << 11) | (4 << 6) | (5 << 1) | 1);
    EXPECT_EQ(TestUtils::GetBE16(out.data, 4), 1);
    EXPECT_EQ(TestUtils::GetBE16(out.data, 6) & 1, 0);
    EXPECT_EQ(TestUtils::GetBE16(out.data, 8) & 1, 1);
}

TEST(GXTexture, IndexedDecodeUsesTheTlut) {
    std::vector<uint8_t> tiled(TiledSize(TexFormat::C4, 8, 8), 0);
    tiled[0] = 0x12;
    std::vector<uint8_t> tlut(16 * 2, 0);
    TestUtils::PutBE16(tlut, 2, 0xFC00);
    TestUtils::PutBE16(tlut, 4, 0x83E0);
    const auto rgba = DecodeRGBA8(TexFormat::C4, tiled.data(), tiled.size(), 8, 8, tlut.data(), 16);
    EXPECT_EQ(rgba[0], 255);
    EXPECT_EQ(rgba[1], 0);
    EXPECT_EQ(rgba[4], 0);
    EXPECT_EQ(rgba[5], 255);
}

TEST(GXTexture, CmprDecodesFourAndThreeColourBlocks) {
    std::vector<uint8_t> tex(TiledSize(TexFormat::CMPR, 8, 8), 0);
    TestUtils::PutBE16(tex, 0, 0xF800);
    TestUtils::PutBE16(tex, 2, 0x001F);
    TestUtils::PutBE32(tex, 4, 0x1B000000);
    TestUtils::PutBE16(tex, 8, 0x001F);
    TestUtils::PutBE16(tex, 10, 0xF800);
    TestUtils::PutBE32(tex, 12, 0xC0000000);

    const auto rgba = DecodeRGBA8(TexFormat::CMPR, tex.data(), tex.size(), 8, 8);
    auto at = [&](uint32_t x, uint32_t y) { return &rgba[(y * 8 + x) * 4]; };
    EXPECT_EQ(at(0, 0)[0], 255);
    EXPECT_EQ(at(0, 0)[2], 0);
    EXPECT_EQ(at(1, 0)[0], 0);
    EXPECT_EQ(at(1, 0)[2], 255);
    EXPECT_EQ(at(2, 0)[0], (255 * 5) >> 3);
    EXPECT_EQ(at(2, 0)[2], (255 * 3) >> 3);
    EXPECT_EQ(at(3, 0)[0], (255 * 3) >> 3);
    EXPECT_EQ(at(3, 0)[2], (255 * 5) >> 3);
    EXPECT_EQ(at(3, 0)[3], 255);

    EXPECT_EQ(at(4, 0)[3], 0);
    EXPECT_EQ(at(4, 0)[0], 127);
    EXPECT_EQ(at(5, 0)[3], 255);
    EXPECT_EQ(at(5, 0)[2], 255);
}

TEST(GXTexture, ParsesN64FormatNames) {
    EXPECT_EQ(ParseN64Format("CI4"), N64Format::CI4);
    EXPECT_EQ(ParseN64Format("IA16"), N64Format::IA16);
    EXPECT_FALSE(ParseN64Format("IA4").has_value());
    EXPECT_EQ(SourceFormatFor(N64Format::IA8), TexFormat::IA4);
    EXPECT_EQ(SourceFormatFor(N64Format::RGBA16), TexFormat::RGB5A3);
}
