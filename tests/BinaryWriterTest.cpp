#include <gtest/gtest.h>
#include "lib/binarytools/BinaryWriter.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include <vector>

TEST(BinaryWriterTest, WriteInt8) {
    LUS::BinaryWriter writer;
    writer.Write(static_cast<int8_t>(0x7F));
    auto vec = writer.ToVector();
    ASSERT_EQ(vec.size(), 1u);
    EXPECT_EQ(static_cast<uint8_t>(vec[0]), 0x7F);
}

TEST(BinaryWriterTest, WriteUInt8) {
    LUS::BinaryWriter writer;
    writer.Write(static_cast<uint8_t>(0xFF));
    auto vec = writer.ToVector();
    ASSERT_EQ(vec.size(), 1u);
    EXPECT_EQ(static_cast<uint8_t>(vec[0]), 0xFF);
}

TEST(BinaryWriterTest, WriteInt16BigEndian) {
    LUS::BinaryWriter writer;
    writer.SetEndianness(Torch::Endianness::Big);
    writer.Write(static_cast<int16_t>(0x0102));
    auto vec = writer.ToVector();
    ASSERT_EQ(vec.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(vec[0]), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(vec[1]), 0x02);
}

TEST(BinaryWriterTest, WriteUInt32BigEndian) {
    LUS::BinaryWriter writer;
    writer.SetEndianness(Torch::Endianness::Big);
    writer.Write(static_cast<uint32_t>(0xDEADBEEF));
    auto vec = writer.ToVector();
    ASSERT_EQ(vec.size(), 4u);
    EXPECT_EQ(static_cast<uint8_t>(vec[0]), 0xDE);
    EXPECT_EQ(static_cast<uint8_t>(vec[1]), 0xAD);
    EXPECT_EQ(static_cast<uint8_t>(vec[2]), 0xBE);
    EXPECT_EQ(static_cast<uint8_t>(vec[3]), 0xEF);
}

TEST(BinaryWriterTest, WriteFloat) {
    LUS::BinaryWriter writer;
    writer.SetEndianness(Torch::Endianness::Big);
    writer.Write(1.0f);
    auto vec = writer.ToVector();
    ASSERT_EQ(vec.size(), 4u);
    // IEEE 754: 1.0f = 0x3F800000 big-endian
    EXPECT_EQ(static_cast<uint8_t>(vec[0]), 0x3F);
    EXPECT_EQ(static_cast<uint8_t>(vec[1]), 0x80);
    EXPECT_EQ(static_cast<uint8_t>(vec[2]), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(vec[3]), 0x00);
}

TEST(BinaryWriterTest, RoundTripInt32) {
    LUS::BinaryWriter writer;
    writer.SetEndianness(Torch::Endianness::Big);
    writer.Write(static_cast<int32_t>(123456));
    auto vec = writer.ToVector();

    std::vector<uint8_t> buf(vec.begin(), vec.end());
    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadInt32(), 123456);
}

TEST(BinaryWriterTest, RoundTripUInt32) {
    LUS::BinaryWriter writer;
    writer.SetEndianness(Torch::Endianness::Big);
    writer.Write(static_cast<uint32_t>(0xCAFEBABE));
    auto vec = writer.ToVector();

    std::vector<uint8_t> buf(vec.begin(), vec.end());
    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadUInt32(), 0xCAFEBABE);
}

TEST(BinaryWriterTest, RoundTripFloat) {
    LUS::BinaryWriter writer;
    writer.SetEndianness(Torch::Endianness::Big);
    writer.Write(3.14f);
    auto vec = writer.ToVector();

    std::vector<uint8_t> buf(vec.begin(), vec.end());
    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);
    EXPECT_FLOAT_EQ(reader.ReadFloat(), 3.14f);
}

TEST(BinaryWriterTest, RoundTripMultipleValues) {
    LUS::BinaryWriter writer;
    writer.SetEndianness(Torch::Endianness::Big);
    writer.Write(static_cast<uint8_t>(0xAA));
    writer.Write(static_cast<uint16_t>(0x1234));
    writer.Write(static_cast<uint32_t>(0xDEADBEEF));
    auto vec = writer.ToVector();

    ASSERT_EQ(vec.size(), 7u); // 1 + 2 + 4

    std::vector<uint8_t> buf(vec.begin(), vec.end());
    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);
    EXPECT_EQ(reader.ReadUByte(), 0xAA);
    EXPECT_EQ(reader.ReadUInt16(), 0x1234);
    EXPECT_EQ(reader.ReadUInt32(), 0xDEADBEEF);
}

TEST(BinaryWriterTest, GetLength) {
    LUS::BinaryWriter writer;
    EXPECT_EQ(writer.GetLength(), 0u);
    writer.Write(static_cast<uint32_t>(0));
    EXPECT_EQ(writer.GetLength(), 4u);
    writer.Write(static_cast<uint8_t>(0));
    EXPECT_EQ(writer.GetLength(), 5u);
}
