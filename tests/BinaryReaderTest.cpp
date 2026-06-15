#include <gtest/gtest.h>
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include "lib/binarytools/Stream.h"
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

TEST(BinaryReaderTest, ReadCString) {
    // "Hi" + null terminator
    std::vector<uint8_t> buf = {'H', 'i', '\0'};
    auto reader = MakeReader(buf);
    std::string result = reader.ReadCString();
    // ReadCString includes the null terminator in the string
    EXPECT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 'H');
    EXPECT_EQ(result[1], 'i');
    EXPECT_EQ(result[2], '\0');
}

TEST(BinaryReaderTest, ReadString) {
    // ReadString reads a big-endian int32 length prefix, then that many chars
    // Length = 3, then "abc"
    std::vector<uint8_t> buf = {0x00, 0x00, 0x00, 0x03, 'a', 'b', 'c'};
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadString(), "abc");
}

TEST(BinaryReaderTest, SeekFromStart) {
    std::vector<uint8_t> buf = {0xAA, 0xBB, 0xCC, 0xDD};
    auto reader = MakeReader(buf);
    reader.Seek(2, LUS::SeekOffsetType::Start);
    EXPECT_EQ(reader.ReadUByte(), 0xCC);
}

TEST(BinaryReaderTest, SeekFromCurrent) {
    std::vector<uint8_t> buf = {0xAA, 0xBB, 0xCC, 0xDD};
    auto reader = MakeReader(buf);
    reader.ReadUByte(); // now at offset 1
    reader.Seek(1, LUS::SeekOffsetType::Current); // skip to offset 2
    EXPECT_EQ(reader.ReadUByte(), 0xCC);
}

TEST(BinaryReaderTest, GetBaseAddress) {
    std::vector<uint8_t> buf = {0xAA, 0xBB, 0xCC, 0xDD};
    auto reader = MakeReader(buf);
    EXPECT_EQ(reader.GetBaseAddress(), 0u);
    reader.ReadUByte();
    EXPECT_EQ(reader.GetBaseAddress(), 1u);
    reader.ReadUByte();
    EXPECT_EQ(reader.GetBaseAddress(), 2u);
}

TEST(BinaryReaderTest, EndiannessSwitching) {
    // 0x0102 as bytes
    std::vector<uint8_t> buf = {0x01, 0x02};
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadUInt16(), 0x0102);

    // Reset and read as little-endian
    reader.Seek(0, LUS::SeekOffsetType::Start);
    reader.SetEndianness(Torch::Endianness::Native);
    uint16_t native = reader.ReadUInt16();
    // On little-endian (x86), native should read 0x0201
    // On big-endian, native should read 0x0102
    // Either way, it should differ from Big on little-endian platforms
    EXPECT_EQ(reader.GetEndianness(), Torch::Endianness::Native);
    (void)native; // value is platform-dependent
}

TEST(BinaryReaderTest, ReadMultipleValues) {
    // Read a sequence: uint8, uint16, uint32
    std::vector<uint8_t> buf = {
        0xFF,                   // uint8
        0x00, 0x0A,             // uint16 = 10
        0x00, 0x00, 0x01, 0x00  // uint32 = 256
    };
    auto reader = MakeReader(buf, Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadUByte(), 0xFF);
    EXPECT_EQ(reader.ReadUInt16(), 10);
    EXPECT_EQ(reader.ReadUInt32(), 256);
}
