#include <gtest/gtest.h>
#include "factories/mk64/CourseVtx.h"
#include "factories/mk64/Paths.h"
#include "factories/mk64/TrackSections.h"
#include "factories/mk64/SpawnData.h"
#include "factories/mk64/DrivingBehaviour.h"
#include "factories/mk64/ItemCurve.h"
#include <vector>

// CourseVtx
TEST(MK64DataTest, CourseVtxConstruction) {
    MK64::CourseVtx vtx = {{100, 200, 300}, {10, 20}, {255, 128, 64, 255}};
    EXPECT_EQ(vtx.ob[0], 100);
    EXPECT_EQ(vtx.ob[1], 200);
    EXPECT_EQ(vtx.ob[2], 300);
    EXPECT_EQ(vtx.tc[0], 10);
    EXPECT_EQ(vtx.tc[1], 20);
    EXPECT_EQ(vtx.cn[0], 255);
    EXPECT_EQ(vtx.cn[1], 128);
    EXPECT_EQ(vtx.cn[2], 64);
    EXPECT_EQ(vtx.cn[3], 255);
}

TEST(MK64DataTest, CourseVtxDataConstruction) {
    std::vector<MK64::CourseVtx> vtxs = {
        {{0, 0, 0}, {0, 0}, {0, 0, 0, 0}},
        {{100, -50, 200}, {32, 64}, {255, 255, 255, 255}},
    };
    MK64::CourseVtxData data(vtxs);
    ASSERT_EQ(data.mVtxs.size(), 2u);
    EXPECT_EQ(data.mVtxs[0].ob[0], 0);
    EXPECT_EQ(data.mVtxs[1].ob[1], -50);
    EXPECT_EQ(data.mVtxs[1].cn[0], 255);
}

TEST(MK64DataTest, CourseVtxFactoryExporters) {
    MK64::CourseVtxFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.SupportModdedAssets());
}

// TrackPath
TEST(MK64DataTest, TrackPathConstruction) {
    MK64::TrackPath path = {100, -200, 300, 5};
    EXPECT_EQ(path.posX, 100);
    EXPECT_EQ(path.posY, -200);
    EXPECT_EQ(path.posZ, 300);
    EXPECT_EQ(path.trackSegment, 5u);
}

TEST(MK64DataTest, PathDataConstruction) {
    std::vector<MK64::TrackPath> paths = {
        {0, 0, 0, 0},
        {1000, -500, 2000, 3},
    };
    MK64::PathData data(paths);
    ASSERT_EQ(data.mPaths.size(), 2u);
    EXPECT_EQ(data.mPaths[1].posX, 1000);
    EXPECT_EQ(data.mPaths[1].trackSegment, 3u);
}

TEST(MK64DataTest, PathsFactoryExporters) {
    MK64::PathsFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.SupportModdedAssets());
}

// TrackSections
TEST(MK64DataTest, TrackSectionsConstruction) {
    MK64::TrackSections sec = {0xDEADBEEF12345678ULL, 2, 7, 0x00FF};
    EXPECT_EQ(sec.crc, 0xDEADBEEF12345678ULL);
    EXPECT_EQ(sec.surfaceType, 2);
    EXPECT_EQ(sec.sectionId, 7);
    EXPECT_EQ(sec.flags, 0x00FFu);
}

TEST(MK64DataTest, TrackSectionsDataConstruction) {
    std::vector<MK64::TrackSections> secs = {
        {0x1111, 0, 0, 0},
        {0x2222, 1, 1, 0x8000},
    };
    MK64::TrackSectionsData data(secs);
    ASSERT_EQ(data.mSecs.size(), 2u);
    EXPECT_EQ(data.mSecs[0].crc, 0x1111u);
    EXPECT_EQ(data.mSecs[1].flags, 0x8000u);
}

TEST(MK64DataTest, TrackSectionsFactoryExporters) {
    MK64::TrackSectionsFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.SupportModdedAssets());
}

// ActorSpawnData
TEST(MK64DataTest, ActorSpawnDataConstruction) {
    MK64::ActorSpawnData spawn = {100, -200, 300, 42};
    EXPECT_EQ(spawn.x, 100);
    EXPECT_EQ(spawn.y, -200);
    EXPECT_EQ(spawn.z, 300);
    EXPECT_EQ(spawn.id, 42u);
}

TEST(MK64DataTest, SpawnDataDataConstruction) {
    std::vector<MK64::ActorSpawnData> spawns = {
        {0, 0, 0, 1},
        {500, 100, -300, 10},
    };
    MK64::SpawnDataData data(spawns);
    ASSERT_EQ(data.mSpawns.size(), 2u);
    EXPECT_EQ(data.mSpawns[1].id, 10u);
}

TEST(MK64DataTest, SpawnDataFactoryExporters) {
    MK64::SpawnDataFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.SupportModdedAssets());
}

// DrivingBehaviour
TEST(MK64DataTest, BhvRawConstruction) {
    MK64::BhvRaw bhv = {10, 20, 0x12345678};
    EXPECT_EQ(bhv.waypoint1, 10);
    EXPECT_EQ(bhv.waypoint2, 20);
    EXPECT_EQ(bhv.bhv, 0x12345678);
}

TEST(MK64DataTest, DrivingDataConstruction) {
    std::vector<MK64::BhvRaw> bhvs = {
        {0, 5, 100},
        {5, 10, -1},
    };
    MK64::DrivingData data(bhvs);
    ASSERT_EQ(data.mBhvs.size(), 2u);
    EXPECT_EQ(data.mBhvs[0].waypoint2, 5);
    EXPECT_EQ(data.mBhvs[1].bhv, -1);
}

TEST(MK64DataTest, DrivingBehaviourFactoryExporters) {
    MK64::DrivingBehaviourFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
}

// ItemCurve
TEST(MK64DataTest, ItemCurveDataConstruction) {
    std::vector<uint8_t> items(100, 0x0A);  // 10x10 probability array
    MK64::ItemCurveData data(items);
    ASSERT_EQ(data.mItems.size(), 100u);
    EXPECT_EQ(data.mItems[0], 0x0A);
    EXPECT_EQ(data.mItems[99], 0x0A);
}

TEST(MK64DataTest, ItemCurveFactoryExporters) {
    MK64::ItemCurveFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.SupportModdedAssets());
}
