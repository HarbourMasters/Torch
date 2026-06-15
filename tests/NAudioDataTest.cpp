#include <gtest/gtest.h>
#include "factories/naudio/v1/AudioTableFactory.h"
#include "factories/naudio/v1/EnvelopeFactory.h"
#include "factories/naudio/v1/AudioContext.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include <vector>

// AudioTableEntry
TEST(NAudioDataTest, AudioTableEntryConstruction) {
    AudioTableEntry entry = {0x1000, 0x2000, 2, 1, 100, 200, 300, 0xDEADBEEF};
    EXPECT_EQ(entry.addr, 0x1000u);
    EXPECT_EQ(entry.size, 0x2000u);
    EXPECT_EQ(entry.medium, 2);
    EXPECT_EQ(entry.cachePolicy, 1);
    EXPECT_EQ(entry.shortData1, 100);
    EXPECT_EQ(entry.shortData2, 200);
    EXPECT_EQ(entry.shortData3, 300);
    EXPECT_EQ(entry.crc, 0xDEADBEEFu);
}

// AudioTableData
TEST(NAudioDataTest, AudioTableDataConstruction) {
    std::vector<AudioTableEntry> entries = {
        {0x1000, 0x100, 0, 0, 0, 0, 0, 0},
        {0x2000, 0x200, 2, 1, 0, 0, 0, 0},
    };
    AudioTableData data(3, 0x5000, AudioTableType::FONT_TABLE, entries);
    EXPECT_EQ(data.medium, 3);
    EXPECT_EQ(data.addr, 0x5000u);
    EXPECT_EQ(data.type, AudioTableType::FONT_TABLE);
    ASSERT_EQ(data.entries.size(), 2u);
    EXPECT_EQ(data.entries[0].addr, 0x1000u);
    EXPECT_EQ(data.entries[1].size, 0x200u);
}

// AudioTableType enum
TEST(NAudioDataTest, AudioTableTypeValues) {
    // Just verify the enum values exist and are distinct
    EXPECT_NE(AudioTableType::SAMPLE_TABLE, AudioTableType::SEQ_TABLE);
    EXPECT_NE(AudioTableType::SEQ_TABLE, AudioTableType::FONT_TABLE);
    EXPECT_NE(AudioTableType::SAMPLE_TABLE, AudioTableType::FONT_TABLE);
}

// EnvelopePoint
TEST(NAudioDataTest, EnvelopePointConstruction) {
    EnvelopePoint pt = {100, -50};
    EXPECT_EQ(pt.delay, 100);
    EXPECT_EQ(pt.arg, -50);
}

// EnvelopeData
TEST(NAudioDataTest, EnvelopeDataConstruction) {
    std::vector<EnvelopePoint> points = {{10, 127}, {20, 0}, {-1, 0}};
    EnvelopeData data(points);
    ASSERT_EQ(data.points.size(), 3u);
    EXPECT_EQ(data.points[0].delay, 10);
    EXPECT_EQ(data.points[0].arg, 127);
    EXPECT_EQ(data.points[2].delay, -1);
}

// TunedSample
TEST(NAudioDataTest, TunedSampleConstruction) {
    TunedSample ts = {0x1234, 0, 1.0f};
    EXPECT_EQ(ts.sample, 0x1234u);
    EXPECT_EQ(ts.sampleBankId, 0u);
    EXPECT_FLOAT_EQ(ts.tuning, 1.0f);
}

// BinaryReader parse test — AudioTable on-disk format
// Header: i16 count + i16 medium + u32 addr + 8 pad bytes = 16 bytes
// Per-entry: u32 addr + u32 size + i8 medium + i8 policy + i16 sd1 + i16 sd2 + i16 sd3 = 16 bytes
TEST(NAudioDataTest, ManualAudioTableParse) {
    std::vector<uint8_t> buf = {
        // Header
        0x00, 0x02,              // count = 2
        0x00, 0x03,              // medium = 3
        0x00, 0x00, 0x50, 0x00,  // addr = 0x5000
        0x00, 0x00, 0x00, 0x00,  // pad
        0x00, 0x00, 0x00, 0x00,  // pad
        // Entry 0: addr=0x1000, size=0x100, medium=0, policy=0, sd1=0, sd2=0, sd3=0
        0x00, 0x00, 0x10, 0x00,
        0x00, 0x00, 0x01, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Entry 1: addr=0x2000, size=0x200, medium=2, policy=1, sd1=10, sd2=20, sd3=30
        0x00, 0x00, 0x20, 0x00,
        0x00, 0x00, 0x02, 0x00,
        0x02, 0x01,
        0x00, 0x0A, 0x00, 0x14, 0x00, 0x1E,
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    // Read header
    auto count = reader.ReadInt16();
    auto medium = reader.ReadInt16();
    auto addr = reader.ReadUInt32();
    reader.Seek(16, LUS::SeekOffsetType::Start); // skip pad to offset 16

    EXPECT_EQ(count, 2);
    EXPECT_EQ(medium, 3);
    EXPECT_EQ(addr, 0x5000u);

    // Read entries
    std::vector<AudioTableEntry> entries;
    for (int i = 0; i < count; i++) {
        auto eAddr = reader.ReadUInt32();
        auto eSize = reader.ReadUInt32();
        auto eMedium = reader.ReadInt8();
        auto ePolicy = reader.ReadInt8();
        auto eSd1 = reader.ReadInt16();
        auto eSd2 = reader.ReadInt16();
        auto eSd3 = reader.ReadInt16();
        entries.push_back(AudioTableEntry{ eAddr, eSize, eMedium, ePolicy, eSd1, eSd2, eSd3, 0 });
    }

    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].addr, 0x1000u);
    EXPECT_EQ(entries[0].size, 0x100u);
    EXPECT_EQ(entries[1].addr, 0x2000u);
    EXPECT_EQ(entries[1].size, 0x200u);
    EXPECT_EQ(entries[1].medium, 2);
    EXPECT_EQ(entries[1].cachePolicy, 1);
    EXPECT_EQ(entries[1].shortData1, 10);
    EXPECT_EQ(entries[1].shortData2, 20);
    EXPECT_EQ(entries[1].shortData3, 30);
}

// NAudioDrivers enum
TEST(NAudioDataTest, NAudioDriversValues) {
    EXPECT_NE(NAudioDrivers::SF64, NAudioDrivers::FZEROX);
    EXPECT_NE(NAudioDrivers::FZEROX, NAudioDrivers::UNKNOWN);
}
