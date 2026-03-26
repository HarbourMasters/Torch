#include <gtest/gtest.h>
#include "factories/pm64/ShapeFactory.h"
#include "factories/pm64/EntityGfxFactory.h"
#include <vector>

// PM64DisplayListInfo
TEST(PM64DataTest, DisplayListInfoConstruction) {
    PM64DisplayListInfo dlInfo = {0x1000, {0xDE000000, 0x06001000, 0xDF000000, 0x00000000}};
    EXPECT_EQ(dlInfo.offset, 0x1000u);
    ASSERT_EQ(dlInfo.commands.size(), 4u);
    EXPECT_EQ(dlInfo.commands[0], 0xDE000000u);
}

// PM64ShapeData
TEST(PM64DataTest, ShapeDataConstruction) {
    std::vector<uint8_t> buffer = {0x00, 0x01, 0x02, 0x03};
    std::vector<PM64DisplayListInfo> dls = {
        {0x100, {0xDE000000, 0x00000000}},
    };
    PM64ShapeData shape(std::move(buffer), std::move(dls), 0x200, 0x80);

    ASSERT_EQ(shape.mBuffer.size(), 4u);
    EXPECT_EQ(shape.mBuffer[0], 0x00);
    ASSERT_EQ(shape.mDisplayLists.size(), 1u);
    EXPECT_EQ(shape.mDisplayLists[0].offset, 0x100u);
    EXPECT_EQ(shape.mVertexTableOffset, 0x200u);
    EXPECT_EQ(shape.mVertexDataSize, 0x80u);
}

TEST(PM64DataTest, ShapeFactoryExporters) {
    PM64ShapeFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Modding).has_value());
}

// PM64EntityDisplayListInfo
TEST(PM64DataTest, EntityDisplayListInfoConstruction) {
    PM64EntityDisplayListInfo dlInfo = {0x500, {0xE7000000, 0x00000000}};
    EXPECT_EQ(dlInfo.offset, 0x500u);
    ASSERT_EQ(dlInfo.commands.size(), 2u);
}

// PM64EntityGfxData
TEST(PM64DataTest, EntityGfxDataConstruction) {
    std::vector<uint8_t> buffer(64, 0xAA);
    std::vector<PM64EntityDisplayListInfo> dls = {
        {0, {0xDE000000, 0x06001000}},
        {0x40, {0xDF000000, 0x00000000}},
    };
    PM64EntityGfxData gfx(std::move(buffer), std::move(dls));

    ASSERT_EQ(gfx.mBuffer.size(), 64u);
    ASSERT_EQ(gfx.mDisplayLists.size(), 2u);
    EXPECT_EQ(gfx.mDisplayLists[1].offset, 0x40u);
    EXPECT_TRUE(gfx.mStandaloneMtx.empty());
}

TEST(PM64DataTest, EntityGfxFactoryExporters) {
    PM64EntityGfxFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Modding).has_value());
}
