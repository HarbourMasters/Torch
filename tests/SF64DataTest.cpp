#include <gtest/gtest.h>
#include "factories/sf64/SkeletonFactory.h"
#include "factories/sf64/MessageFactory.h"
#include <vector>

// SF64 LimbData
TEST(SF64DataTest, LimbDataConstruction) {
    SF64::LimbData limb(0x06001000, 0x06002000, Vec3f(1.0f, 2.0f, 3.0f), Vec3s(10, 20, 30), 0x06003000, 0x06004000, 5);
    EXPECT_EQ(limb.mAddr, 0x06001000u);
    EXPECT_EQ(limb.mDList, 0x06002000u);
    EXPECT_FLOAT_EQ(limb.mTrans.x, 1.0f);
    EXPECT_FLOAT_EQ(limb.mTrans.y, 2.0f);
    EXPECT_FLOAT_EQ(limb.mTrans.z, 3.0f);
    EXPECT_EQ(limb.mRot.x, 10);
    EXPECT_EQ(limb.mRot.y, 20);
    EXPECT_EQ(limb.mRot.z, 30);
    EXPECT_EQ(limb.mSibling, 0x06003000u);
    EXPECT_EQ(limb.mChild, 0x06004000u);
    EXPECT_EQ(limb.mIndex, 5);
}

// SF64 SkeletonData
TEST(SF64DataTest, SkeletonDataConstruction) {
    std::vector<SF64::LimbData> limbs;
    limbs.emplace_back(0x1000, 0x2000, Vec3f(0, 0, 0), Vec3s(0, 0, 0), 0, 0x1001, 0);
    limbs.emplace_back(0x1001, 0x2001, Vec3f(10.0f, 0, 0), Vec3s(0, 0, 0), 0, 0, 1);

    SF64::SkeletonData skel(limbs);
    ASSERT_EQ(skel.mSkeleton.size(), 2u);
    EXPECT_EQ(skel.mSkeleton[0].mAddr, 0x1000u);
    EXPECT_EQ(skel.mSkeleton[1].mIndex, 1);
    EXPECT_FLOAT_EQ(skel.mSkeleton[1].mTrans.x, 10.0f);
}

TEST(SF64DataTest, SkeletonDataEmpty) {
    std::vector<SF64::LimbData> limbs;
    SF64::SkeletonData skel(limbs);
    EXPECT_TRUE(skel.mSkeleton.empty());
}

TEST(SF64DataTest, SkeletonFactoryExporters) {
    SF64::SkeletonFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
}

// SF64 MessageData
TEST(SF64DataTest, MessageDataConstruction) {
    std::vector<uint16_t> opcodes = {0x20, 0x21, 0x22, 0x01, 0x00};  // "ABC\nEND"
    std::string mesgStr = "ABC\n";
    SF64::MessageData msg(opcodes, mesgStr);
    ASSERT_EQ(msg.mMessage.size(), 5u);
    EXPECT_EQ(msg.mMessage[0], 0x20);
    EXPECT_EQ(msg.mMessage[4], 0x00);  // END_CODE
    EXPECT_EQ(msg.mMesgStr, "ABC\n");
}

TEST(SF64DataTest, MessageDataEmpty) {
    std::vector<uint16_t> opcodes = {0x00};  // just END
    SF64::MessageData msg(opcodes, "");
    ASSERT_EQ(msg.mMessage.size(), 1u);
    EXPECT_TRUE(msg.mMesgStr.empty());
}

TEST(SF64DataTest, MessageFactoryExporters) {
    SF64::MessageFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Modding).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::XML).has_value());
}

TEST(SF64DataTest, MessageFactorySupportsModding) {
    SF64::MessageFactory factory;
    EXPECT_TRUE(factory.SupportModdedAssets());
}
