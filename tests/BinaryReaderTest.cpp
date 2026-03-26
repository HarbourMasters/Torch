#include <gtest/gtest.h>
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include <vector>
#include <cstring>

// Helper to create a BinaryReader from a vector of bytes
static LUS::BinaryReader MakeReader(std::vector<uint8_t>& buf, Torch::Endianness endianness = Torch::Endianness::Big) {
    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(endianness);
    return reader;
}

TEST(BinaryReaderTest, ReadInt8) {
    std::vector<uint8_t> buf = {0x7F};
    auto reader = MakeReader(buf);
    EXPECT_EQ(reader.ReadInt8(), 0x7F);
}

TEST(BinaryReaderTest, ReadInt8Negative) {
    std::vector<uint8_t> buf = {0x80};
    auto reader = MakeReader(buf);
    EXPECT_EQ(reader.ReadInt8(), -128);
}

TEST(BinaryReaderTest, ReadUByte) {
    std::vector<uint8_t> buf = {0xFF};
    auto reader = MakeReader(buf);
    EXPECT_EQ(reader.ReadUByte(), 0xFF);
}

TEST(BinaryReaderTest, ReadInt16BigEndian) {
    std::vector<uint8_t> buf = {0x01, 0x00};
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadInt16(), 256);
}

TEST(BinaryReaderTest, ReadUInt16BigEndian) {
    std::vector<uint8_t> buf = {0xFF, 0xFE};
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadUInt16(), 0xFFFE);
}

TEST(BinaryReaderTest, ReadInt32BigEndian) {
    std::vector<uint8_t> buf = {0x00, 0x01, 0x00, 0x00};
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadInt32(), 0x00010000);
}

TEST(BinaryReaderTest, ReadUInt32BigEndian) {
    std::vector<uint8_t> buf = {0xDE, 0xAD, 0xBE, 0xEF};
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadUInt32(), 0xDEADBEEF);
}

TEST(BinaryReaderTest, ReadUInt64BigEndian) {
    std::vector<uint8_t> buf = {0x00, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadUInt64(), 0x00000000DEADBEEF);
}

TEST(BinaryReaderTest, ReadFloat) {
    // IEEE 754: 1.0f = 0x3F800000
    std::vector<uint8_t> buf = {0x3F, 0x80, 0x00, 0x00};
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_FLOAT_EQ(reader.ReadFloat(), 1.0f);
}

TEST(BinaryReaderTest, GetLength) {
    std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04};
    auto reader = MakeReader(buf);
    EXPECT_EQ(reader.GetLength(), 4u);
}
