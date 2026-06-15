#include <gtest/gtest.h>
#include "factories/mk64/CourseVtx.h"
#include "factories/mk64/Paths.h"
#include "factories/mk64/TrackSections.h"
#include "factories/mk64/SpawnData.h"
#include "factories/mk64/DrivingBehaviour.h"
#include "factories/mk64/ItemCurve.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
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

// ============================================================
// BinaryReader parse tests — replicate factory parse logic
// ============================================================

// CourseVtx parse: flag extraction from color bytes
// cn1 low 2 bits and cn2 bits 2-3 encode flags; colors are masked
TEST(MK64DataTest, ManualCourseVtxFlagExtraction) {
    // Craft a 14-byte big-endian CourseVtx:
    // ob: (50, -100, 200), tc: (10, 20), cn: (0x07, 0x0B, 0xAA, 0xFF)
    // Expected flags: (0x07 & 3) | ((0x0B << 2) & 0xC) = 3 | 0xC = 0xF
    // Expected colors: (0x07 & 0xFC)=0x04, (0x0B & 0xFC)=0x08, 0xAA, 0xFF→0xFF
    std::vector<uint8_t> buf = {
        0x00, 0x32,  // ob[0] = 50
        0xFF, 0x9C,  // ob[1] = -100
        0x00, 0xC8,  // ob[2] = 200
        0x00, 0x0A,  // tc[0] = 10
        0x00, 0x14,  // tc[1] = 20
        0x07,        // cn[0] = 0x07
        0x0B,        // cn[1] = 0x0B
        0xAA,        // cn[2] = 0xAA
        0xFF,        // cn[3] = 0xFF
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    auto x = reader.ReadInt16();
    auto y = reader.ReadInt16();
    auto z = reader.ReadInt16();
    auto tc1 = reader.ReadInt16();
    auto tc2 = reader.ReadInt16();
    auto cn1 = reader.ReadUByte();
    auto cn2 = reader.ReadUByte();
    auto cn3 = reader.ReadUByte();
    auto cn4 = reader.ReadUByte();

    uint16_t flags = cn1 & 3;
    flags |= (cn2 << 2) & 0xC;

    VtxRaw vtx = {{ x, y, z }, flags, { tc1, tc2 }, { (uint8_t)(cn1 & 0xfc), (uint8_t)(cn2 & 0xfc), cn3, 0xff }};

    EXPECT_EQ(vtx.ob[0], 50);
    EXPECT_EQ(vtx.ob[1], -100);
    EXPECT_EQ(vtx.ob[2], 200);
    EXPECT_EQ(vtx.flag, 0xFu);         // flags = 3 | 0xC
    EXPECT_EQ(vtx.cn[0], 0x04);        // 0x07 & 0xFC
    EXPECT_EQ(vtx.cn[1], 0x08);        // 0x0B & 0xFC
    EXPECT_EQ(vtx.cn[2], 0xAA);
    EXPECT_EQ(vtx.cn[3], 0xFF);        // always 0xFF
}

// DrivingBehaviour parse: sentinel-terminated reading
TEST(MK64DataTest, ManualDrivingBehaviourSentinel) {
    // Two normal entries + sentinel (w1=-1, w2=-1, id=0)
    // Each entry: int16_t w1, int16_t w2, int32_t id = 8 bytes
    std::vector<uint8_t> buf = {
        // Entry 0: w1=5, w2=10, id=0x100
        0x00, 0x05,  0x00, 0x0A,  0x00, 0x00, 0x01, 0x00,
        // Entry 1: w1=15, w2=20, id=0x200
        0x00, 0x0F,  0x00, 0x14,  0x00, 0x00, 0x02, 0x00,
        // Sentinel: w1=-1, w2=-1, id=0
        0xFF, 0xFF,  0xFF, 0xFF,  0x00, 0x00, 0x00, 0x00,
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<MK64::BhvRaw> behaviours;
    while (true) {
        auto w1 = reader.ReadInt16();
        auto w2 = reader.ReadInt16();
        auto id = reader.ReadInt32();
        behaviours.push_back(MK64::BhvRaw({ w1, w2, id }));
        if ((w1 == -1) && (w2 == -1)) {
            break;
        }
    }

    ASSERT_EQ(behaviours.size(), 3u);
    EXPECT_EQ(behaviours[0].waypoint1, 5);
    EXPECT_EQ(behaviours[0].waypoint2, 10);
    EXPECT_EQ(behaviours[0].bhv, 0x100);
    EXPECT_EQ(behaviours[1].waypoint1, 15);
    EXPECT_EQ(behaviours[1].bhv, 0x200);
    EXPECT_EQ(behaviours[2].waypoint1, -1);
    EXPECT_EQ(behaviours[2].waypoint2, -1);
}

// TrackSections parse: on-disk 8 bytes vs struct 16 bytes (u64 crc field)
TEST(MK64DataTest, ManualTrackSectionsParse) {
    // On-disk per entry: u32 addr + i8 surf + i8 sect + u16 flags = 8 bytes
    // The u32 addr maps to the u64 crc field in the struct
    std::vector<uint8_t> buf = {
        // Entry 0: addr=0xDEADBEEF, surf=2, sect=7, flags=0x00FF
        0xDE, 0xAD, 0xBE, 0xEF,  0x02, 0x07,  0x00, 0xFF,
        // Entry 1: addr=0x12345678, surf=-1, sect=3, flags=0x8000
        0x12, 0x34, 0x56, 0x78,  0xFF, 0x03,  0x80, 0x00,
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<MK64::TrackSections> sections;
    for (int i = 0; i < 2; i++) {
        auto addr = reader.ReadUInt32();
        auto surf = reader.ReadInt8();
        auto sect = reader.ReadInt8();
        auto flags = reader.ReadUInt16();
        sections.push_back(MK64::TrackSections({ addr, surf, sect, flags }));
    }

    ASSERT_EQ(sections.size(), 2u);
    EXPECT_EQ(sections[0].crc, 0xDEADBEEFu);
    EXPECT_EQ(sections[0].surfaceType, 2);
    EXPECT_EQ(sections[0].sectionId, 7);
    EXPECT_EQ(sections[0].flags, 0x00FFu);
    EXPECT_EQ(sections[1].crc, 0x12345678u);
    EXPECT_EQ(sections[1].surfaceType, -1);
    EXPECT_EQ(sections[1].sectionId, 3);
    EXPECT_EQ(sections[1].flags, 0x8000u);
}

// Paths parse: 8 bytes per entry (3×int16_t + uint16_t)
TEST(MK64DataTest, ManualPathsParse) {
    std::vector<uint8_t> buf = {
        // Entry 0: x=1000, y=-500, z=2000, segment=3
        0x03, 0xE8,  0xFE, 0x0C,  0x07, 0xD0,  0x00, 0x03,
        // Entry 1: x=0, y=0, z=0, segment=0
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<MK64::TrackPath> paths;
    for (int i = 0; i < 2; i++) {
        auto x = reader.ReadInt16();
        auto y = reader.ReadInt16();
        auto z = reader.ReadInt16();
        auto seg = reader.ReadUInt16();
        paths.push_back(MK64::TrackPath({ x, y, z, seg }));
    }

    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0].posX, 1000);
    EXPECT_EQ(paths[0].posY, -500);
    EXPECT_EQ(paths[0].posZ, 2000);
    EXPECT_EQ(paths[0].trackSegment, 3u);
    EXPECT_EQ(paths[1].posX, 0);
}

// SpawnData parse: 8 bytes per entry (3×int16_t + uint16_t)
TEST(MK64DataTest, ManualSpawnDataParse) {
    std::vector<uint8_t> buf = {
        // x=500, y=100, z=-300, id=42
        0x01, 0xF4,  0x00, 0x64,  0xFE, 0xD4,  0x00, 0x2A,
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    auto x = reader.ReadInt16();
    auto y = reader.ReadInt16();
    auto z = reader.ReadInt16();
    auto id = reader.ReadUInt16();
    MK64::ActorSpawnData spawn = { x, y, z, id };

    EXPECT_EQ(spawn.x, 500);
    EXPECT_EQ(spawn.y, 100);
    EXPECT_EQ(spawn.z, -300);
    EXPECT_EQ(spawn.id, 42u);
}

// ItemCurve parse: 100 bytes of uint8_t
TEST(MK64DataTest, ManualItemCurveParse) {
    // 10x10 probability array
    std::vector<uint8_t> buf(100);
    for (int i = 0; i < 100; i++) {
        buf[i] = static_cast<uint8_t>(i);
    }

    LUS::BinaryReader reader(buf.data(), buf.size());
    std::vector<uint8_t> items;
    for (int i = 0; i < 100; i++) {
        items.push_back(reader.ReadUByte());
    }

    ASSERT_EQ(items.size(), 100u);
    EXPECT_EQ(items[0], 0);
    EXPECT_EQ(items[50], 50);
    EXPECT_EQ(items[99], 99);
}
