#include <gtest/gtest.h>
#include "factories/fzerox/CourseFactory.h"
#include "factories/fzerox/course/Course.h"
#include <vector>

// Struct sizes
TEST(FZXCourseTest, ControlPointSize) {
    EXPECT_EQ(sizeof(FZX::ControlPoint), 0x14u);
}

TEST(FZXCourseTest, CourseRawDataSize) {
    EXPECT_EQ(sizeof(FZX::CourseRawData), 0x7E0u);
}

// CourseData construction
TEST(FZXCourseTest, CourseDataConstruction) {
    FZX::ControlPointInfo cpi = {};
    cpi.controlPoint.pos = {100.0f, 200.0f, 300.0f};
    cpi.controlPoint.radiusLeft = 50;
    cpi.controlPoint.radiusRight = 60;
    cpi.controlPoint.trackSegmentInfo = 0;
    cpi.bankAngle = 10;
    cpi.pit = -1;
    cpi.dash = -1;
    cpi.dirt = -1;
    cpi.ice = -1;
    cpi.jump = -1;
    cpi.landmine = -1;
    cpi.gate = -1;
    cpi.building = -1;
    cpi.sign = -1;

    std::vector<FZX::ControlPointInfo> cpis = {cpi};
    std::vector<char> fileName(23, '\0');
    fileName[0] = 'T'; fileName[1] = 'e'; fileName[2] = 's'; fileName[3] = 't';

    FZX::CourseData course(4, 0, 1, 0, fileName, 5, cpis);
    EXPECT_EQ(course.mCreatorId, 4);
    EXPECT_EQ(course.mVenue, 0);
    EXPECT_EQ(course.mSkybox, 1);
    EXPECT_EQ(course.mFlag, 0);
    EXPECT_EQ(course.mBgm, 5);
    ASSERT_EQ(course.mControlPointInfos.size(), 1u);
    EXPECT_FLOAT_EQ(course.mControlPointInfos[0].controlPoint.pos.x, 100.0f);
}

TEST(FZXCourseTest, CourseDataEmpty) {
    std::vector<FZX::ControlPointInfo> empty;
    std::vector<char> fileName(23, '\0');
    FZX::CourseData course(0, 0, 0, 0, fileName, 0, empty);
    EXPECT_TRUE(course.mControlPointInfos.empty());
}

// CalculateChecksum
TEST(FZXCourseTest, ChecksumEmptyCourse) {
    std::vector<FZX::ControlPointInfo> empty;
    std::vector<char> fileName(23, '\0');
    FZX::CourseData course(0, 0, 0, 0, fileName, 0, empty);
    uint32_t checksum = course.CalculateChecksum();
    // checksum starts with mControlPointInfos.size() = 0
    EXPECT_EQ(checksum, 0u);
}

TEST(FZXCourseTest, ChecksumDeterministic) {
    FZX::ControlPointInfo cpi = {};
    cpi.controlPoint.pos = {10.0f, 20.0f, 30.0f};
    cpi.controlPoint.radiusLeft = 100;
    cpi.controlPoint.radiusRight = 100;
    cpi.controlPoint.trackSegmentInfo = 0x3F; // TRACK_TYPE_NONE
    cpi.bankAngle = 0;
    cpi.pit = 0;
    cpi.dash = 0;
    cpi.dirt = 0;
    cpi.ice = 0;
    cpi.jump = 0;
    cpi.landmine = 0;
    cpi.gate = 0;
    cpi.building = 0;
    cpi.sign = 0;

    std::vector<FZX::ControlPointInfo> cpis = {cpi};
    std::vector<char> fileName(23, '\0');
    FZX::CourseData course(4, 0, 0, 0, fileName, 0, cpis);

    uint32_t checksum1 = course.CalculateChecksum();
    uint32_t checksum2 = course.CalculateChecksum();
    EXPECT_EQ(checksum1, checksum2);
    EXPECT_NE(checksum1, 0u);  // with non-zero position, checksum should be non-zero
}

// Track mask constants
TEST(FZXCourseTest, TrackMaskConstants) {
    EXPECT_EQ(TRACK_JOIN_MASK, 0x600);
    EXPECT_EQ(TRACK_FORM_MASK, 0x00038000);
    EXPECT_EQ(TRACK_FLAG_CONTINUOUS, 0x40000000);
    EXPECT_EQ(TRACK_TYPE_MASK, 0x3F);
    EXPECT_EQ(TRACK_SHAPE_MASK, 0x1C0);
}

// Factory
TEST(FZXCourseTest, CourseFactoryExporters) {
    FZX::CourseFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Modding).has_value());
    EXPECT_TRUE(factory.SupportModdedAssets());
}
