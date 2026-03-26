#include <gtest/gtest.h>
#include "factories/sm64/AnimationFactory.h"
#include <vector>

TEST(SM64AnimationTest, AnimationDataConstruction) {
    std::vector<uint16_t> indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    std::vector<int16_t> entries = {100, 200, 300, -100, -200, -300};

    SM64::AnimationData anim(0, 1, 0, 10, 50, 1, 60, indices, entries);
    EXPECT_EQ(anim.mFlags, 0);
    EXPECT_EQ(anim.mAnimYTransDivisor, 1);
    EXPECT_EQ(anim.mStartFrame, 0);
    EXPECT_EQ(anim.mLoopStart, 10);
    EXPECT_EQ(anim.mLoopEnd, 50);
    EXPECT_EQ(anim.mUnusedBoneCount, 1);
    EXPECT_EQ(anim.mLength, 60);
    ASSERT_EQ(anim.mIndices.size(), 12u);
    ASSERT_EQ(anim.mEntries.size(), 6u);
    EXPECT_EQ(anim.mEntries[3], -100);
}

TEST(SM64AnimationTest, AnimindexCountMacro) {
    // ANIMINDEX_COUNT(boneCount) = (boneCount + 1) * 6
    EXPECT_EQ(ANIMINDEX_COUNT(0), 6);
    EXPECT_EQ(ANIMINDEX_COUNT(1), 12);
    EXPECT_EQ(ANIMINDEX_COUNT(15), 96);
    EXPECT_EQ(ANIMINDEX_COUNT(20), 126);
}

TEST(SM64AnimationTest, AnimationFactoryExporters) {
    SM64::AnimationFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Modding).has_value());
}

TEST(SM64AnimationTest, AnimationDataEmptyVectors) {
    std::vector<uint16_t> indices;
    std::vector<int16_t> entries;
    SM64::AnimationData anim(1, 0, 0, 0, 0, 0, 0, indices, entries);
    EXPECT_EQ(anim.mFlags, 1);
    EXPECT_TRUE(anim.mIndices.empty());
    EXPECT_TRUE(anim.mEntries.empty());
}
