#include <gtest/gtest.h>
#include "utils/Decompressor.h"
#include "factories/BaseFactory.h"
#include <vector>

// GetCompressionType — reads a 4-byte magic string at the given offset

TEST(DecompressorTest, GetCompressionTypeMIO0) {
    std::vector<uint8_t> buf = {'M', 'I', 'O', '0', '\0'};
    EXPECT_EQ(Decompressor::GetCompressionType(buf, 0), CompressionType::None);
    // offset=0 returns None unconditionally (the implementation checks `if (offset)`)
}

TEST(DecompressorTest, GetCompressionTypeMIO0AtOffset) {
    // Place "MIO0" at offset 4
    std::vector<uint8_t> buf = {0, 0, 0, 0, 'M', 'I', 'O', '0', '\0'};
    EXPECT_EQ(Decompressor::GetCompressionType(buf, 4), CompressionType::MIO0);
}

TEST(DecompressorTest, GetCompressionTypeYay0) {
    std::vector<uint8_t> buf = {0, 'Y', 'a', 'y', '0', '\0'};
    EXPECT_EQ(Decompressor::GetCompressionType(buf, 1), CompressionType::YAY0);
}

TEST(DecompressorTest, GetCompressionTypePERS) {
    // "PERS" is also treated as YAY0
    std::vector<uint8_t> buf = {0, 'P', 'E', 'R', 'S', '\0'};
    EXPECT_EQ(Decompressor::GetCompressionType(buf, 1), CompressionType::YAY0);
}

TEST(DecompressorTest, GetCompressionTypeYay1) {
    std::vector<uint8_t> buf = {0, 'Y', 'a', 'y', '1', '\0'};
    EXPECT_EQ(Decompressor::GetCompressionType(buf, 1), CompressionType::YAY1);
}

TEST(DecompressorTest, GetCompressionTypeYaz0) {
    std::vector<uint8_t> buf = {0, 'Y', 'a', 'z', '0', '\0'};
    EXPECT_EQ(Decompressor::GetCompressionType(buf, 1), CompressionType::YAZ0);
}

TEST(DecompressorTest, GetCompressionTypeUnknown) {
    std::vector<uint8_t> buf = {0, 'X', 'Y', 'Z', 'W', '\0'};
    EXPECT_EQ(Decompressor::GetCompressionType(buf, 1), CompressionType::None);
}

TEST(DecompressorTest, GetCompressionTypeZeroOffset) {
    // offset=0 always returns None
    std::vector<uint8_t> buf = {'M', 'I', 'O', '0', '\0'};
    EXPECT_EQ(Decompressor::GetCompressionType(buf, 0), CompressionType::None);
}

// IS_SEGMENTED macro tests (pure bit logic, no Companion dependency)

TEST(DecompressorTest, IsSegmentedMacroTrue) {
    // Segment 0x06, offset 0x001000 → segmented
    EXPECT_TRUE(IS_SEGMENTED(0x06001000));
}

TEST(DecompressorTest, IsSegmentedMacroFalse) {
    // Physical address (high byte 0x00) → not segmented
    EXPECT_FALSE(IS_SEGMENTED(0x00100000));
}

TEST(DecompressorTest, IsSegmentedMacroHighSegment) {
    // Segment 0x20 is out of range (must be < 0x20)
    EXPECT_FALSE(IS_SEGMENTED(0x20000000));
}

TEST(DecompressorTest, SegmentNumberMacro) {
    EXPECT_EQ(SEGMENT_NUMBER(0x06001000), 0x06u);
    EXPECT_EQ(SEGMENT_NUMBER(0x0E123456), 0x0Eu);
}

TEST(DecompressorTest, SegmentOffsetMacro) {
    EXPECT_EQ(SEGMENT_OFFSET(0x06001000), 0x001000u);
    EXPECT_EQ(SEGMENT_OFFSET(0x0EFFFFFF), 0xFFFFFFu);
}

TEST(DecompressorTest, AssetPtrMacroSegmented) {
    // Segmented address → returns just the offset portion
    EXPECT_EQ(ASSET_PTR(0x06001000), 0x001000u);
}

TEST(DecompressorTest, AssetPtrMacroPhysical) {
    // Physical address → returns the address unchanged
    EXPECT_EQ(ASSET_PTR(0x00100000), 0x00100000u);
}

// ClearCache — just verify it doesn't crash
TEST(DecompressorTest, ClearCacheNoCrash) {
    Decompressor::ClearCache();
    // If we get here without crashing, the test passes
}
