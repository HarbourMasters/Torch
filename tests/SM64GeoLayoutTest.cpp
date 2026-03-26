#include <gtest/gtest.h>
#include "factories/sm64/GeoLayoutFactory.h"
#include <vector>

using namespace SM64;

TEST(SM64GeoLayoutTest, GeoCommandConstruction) {
    GeoCommand cmd;
    cmd.opcode = GeoOpcode::End;
    cmd.skipped = false;
    EXPECT_EQ(cmd.opcode, GeoOpcode::End);
    EXPECT_FALSE(cmd.skipped);
    EXPECT_TRUE(cmd.arguments.empty());
}

TEST(SM64GeoLayoutTest, GeoCommandWithArguments) {
    GeoCommand cmd;
    cmd.opcode = GeoOpcode::NodeTranslation;
    cmd.skipped = false;
    cmd.arguments.push_back((uint8_t)0x01);          // drawingLayer
    cmd.arguments.push_back(Vec3s(100, 200, 300));    // translation
    ASSERT_EQ(cmd.arguments.size(), 2u);
    EXPECT_EQ(std::get<uint8_t>(cmd.arguments[0]), 0x01);
    auto v = std::get<Vec3s>(cmd.arguments[1]);
    EXPECT_EQ(v.x, 100);
    EXPECT_EQ(v.y, 200);
    EXPECT_EQ(v.z, 300);
}

TEST(SM64GeoLayoutTest, GeoLayoutConstruction) {
    std::vector<GeoCommand> cmds;
    GeoCommand open;
    open.opcode = GeoOpcode::OpenNode;
    open.skipped = false;
    cmds.push_back(open);

    GeoCommand close;
    close.opcode = GeoOpcode::CloseNode;
    close.skipped = false;
    cmds.push_back(close);

    GeoLayout layout(cmds);
    ASSERT_EQ(layout.commands.size(), 2u);
    EXPECT_EQ(layout.commands[0].opcode, GeoOpcode::OpenNode);
    EXPECT_EQ(layout.commands[1].opcode, GeoOpcode::CloseNode);
}

TEST(SM64GeoLayoutTest, GeoLayoutSkippedCommand) {
    GeoCommand cmd;
    cmd.opcode = GeoOpcode::NOP;
    cmd.skipped = true;
    std::vector<GeoCommand> cmds = {cmd};

    GeoLayout layout(cmds);
    EXPECT_TRUE(layout.commands[0].skipped);
}

TEST(SM64GeoLayoutTest, HasExpectedExporters) {
    GeoLayoutFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Code));
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}

// Test GeoArgument variant with different types
TEST(SM64GeoLayoutTest, GeoArgumentVariantTypes) {
    GeoArgument u8_arg = (uint8_t)42;
    GeoArgument s16_arg = (int16_t)-100;
    GeoArgument u32_arg = (uint32_t)0xDEADBEEF;
    GeoArgument vec3f_arg = Vec3f(1.0f, 2.0f, 3.0f);
    GeoArgument str_arg = std::string("test_symbol");

    EXPECT_EQ(std::get<uint8_t>(u8_arg), 42);
    EXPECT_EQ(std::get<int16_t>(s16_arg), -100);
    EXPECT_EQ(std::get<uint32_t>(u32_arg), 0xDEADBEEFu);
    EXPECT_FLOAT_EQ(std::get<Vec3f>(vec3f_arg).x, 1.0f);
    EXPECT_EQ(std::get<std::string>(str_arg), "test_symbol");
}
