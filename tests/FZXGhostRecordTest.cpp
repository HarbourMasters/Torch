#include <gtest/gtest.h>
#include "factories/fzerox/GhostRecordFactory.h"
#include <vector>
#include <cstring>

// Helper to create a minimal GhostRecordData
static FZX::GhostRecordData MakeGhostRecord(std::vector<int8_t> replayData = {}, std::vector<int32_t> lapTimes = {}) {
    FZX::GhostMachineInfo info = {};
    return FZX::GhostRecordData(
        0, 0, 0, 0, 0, 0, "", info,
        0, std::move(lapTimes), 0, 0, std::move(replayData));
}

// Save_CalculateChecksum
TEST(FZXGhostRecordTest, SaveChecksumKnownBuffer) {
    auto ghost = MakeGhostRecord();
    uint8_t data[] = {0x10, 0x20, 0x30, 0x40};
    uint16_t result = ghost.Save_CalculateChecksum(data, 4);
    EXPECT_EQ(result, 0x10 + 0x20 + 0x30 + 0x40);
}

TEST(FZXGhostRecordTest, SaveChecksumEmpty) {
    auto ghost = MakeGhostRecord();
    uint8_t data[] = {0};
    uint16_t result = ghost.Save_CalculateChecksum(data, 0);
    EXPECT_EQ(result, 0u);
}

TEST(FZXGhostRecordTest, SaveChecksumSingleByte) {
    auto ghost = MakeGhostRecord();
    uint8_t data[] = {0xAB};
    uint16_t result = ghost.Save_CalculateChecksum(data, 1);
    EXPECT_EQ(result, 0xABu);
}

// CalculateReplayChecksum
TEST(FZXGhostRecordTest, ReplayChecksum4Bytes) {
    // 4 bytes = one int32: 0x01020304
    auto ghost = MakeGhostRecord({0x01, 0x02, 0x03, 0x04});
    int32_t result = ghost.CalculateReplayChecksum();
    // byte0 << 24 | byte1 << 16 | byte2 << 8 | byte3
    EXPECT_EQ(result, 0x01020304);
}

TEST(FZXGhostRecordTest, ReplayChecksum8Bytes) {
    auto ghost = MakeGhostRecord({0x01, 0x02, 0x03, 0x04, 0x10, 0x20, 0x30, 0x40});
    int32_t result = ghost.CalculateReplayChecksum();
    EXPECT_EQ(result, 0x01020304 + 0x10203040);
}

TEST(FZXGhostRecordTest, ReplayChecksumNonAligned) {
    // 5 bytes: first 4 form a word, the 5th byte starts accumulating but
    // never completes (i never hits 0 again), so only the first word counts
    auto ghost = MakeGhostRecord({0x01, 0x02, 0x03, 0x04, 0xFF});
    int32_t result = ghost.CalculateReplayChecksum();
    EXPECT_EQ(result, 0x01020304);
}

// GhostMachineInfo
TEST(FZXGhostRecordTest, GhostMachineInfoConstruction) {
    FZX::GhostMachineInfo info = {
        1, 2, 3, 4, 5, 6, 7, 8,  // character through decal
        0xAA, 0xBB, 0xCC,         // bodyR/G/B
        0x11, 0x22, 0x33,         // numberR/G/B
        0x44, 0x55, 0x66,         // decalR/G/B
        0x77, 0x88, 0x99          // cockpitR/G/B
    };
    EXPECT_EQ(info.character, 1);
    EXPECT_EQ(info.bodyR, 0xAA);
    EXPECT_EQ(info.cockpitB, 0x99);
}

// GhostRecordData
TEST(FZXGhostRecordTest, GhostRecordDataConstruction) {
    FZX::GhostMachineInfo info = {};
    std::vector<int32_t> lapTimes = {60000, 61000, 62000};
    std::vector<int8_t> replay = {1, 2, 3, 4, 5};
    FZX::GhostRecordData ghost(
        0x1234, 1, 0x56789ABC, 42, 183000, 0,
        "CustomTrack", info,
        0x5678, lapTimes, -1, 5, replay);

    EXPECT_EQ(ghost.mRecordChecksum, 0x1234u);
    EXPECT_EQ(ghost.mGhostType, 1u);
    EXPECT_EQ(ghost.mReplayChecksum, 0x56789ABC);
    EXPECT_EQ(ghost.mCourseEncoding, 42);
    EXPECT_EQ(ghost.mRaceTime, 183000);
    EXPECT_EQ(ghost.mTrackName, "CustomTrack");
    EXPECT_EQ(ghost.mDataChecksum, 0x5678u);
    ASSERT_EQ(ghost.mLapTimes.size(), 3u);
    EXPECT_EQ(ghost.mLapTimes[0], 60000);
    EXPECT_EQ(ghost.mReplayEnd, -1);
    EXPECT_EQ(ghost.mReplaySize, 5u);
    ASSERT_EQ(ghost.mReplayData.size(), 5u);
}

// Factory
TEST(FZXGhostRecordTest, GhostRecordFactoryExporters) {
    FZX::GhostRecordFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Modding).has_value());
    EXPECT_TRUE(factory.SupportModdedAssets());
}
