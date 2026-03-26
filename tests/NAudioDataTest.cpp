#include <gtest/gtest.h>
#include "factories/naudio/v1/AudioTableFactory.h"
#include "factories/naudio/v1/EnvelopeFactory.h"
#include "factories/naudio/v1/AudioContext.h"
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

// NAudioDrivers enum
TEST(NAudioDataTest, NAudioDriversValues) {
    EXPECT_NE(NAudioDrivers::SF64, NAudioDrivers::FZEROX);
    EXPECT_NE(NAudioDrivers::FZEROX, NAudioDrivers::UNKNOWN);
}
