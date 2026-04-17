#include <gtest/gtest.h>
#include "factories/sm64/GeoLayoutFactory.h"
#include "lib/binarytools/endianness.h"
#include <vector>
#include <cstring>

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

// Byte-level command parsing tests — verify binary format using the same
// BSWAP logic that cur_geo_cmd_* macros use in GeoLayoutFactory::parse

TEST(SM64GeoLayoutTest, ByteLevelEndCommand) {
    // End command: GeoOpcode::End = 1
    uint8_t cmd[] = { 0x01, 0x00, 0x00, 0x00 };
    auto opcode = static_cast<GeoOpcode>(cmd[0]);
    EXPECT_EQ(opcode, GeoOpcode::End);
}

TEST(SM64GeoLayoutTest, ByteLevelOpenCloseNode) {
    // OpenNode: opcode 0x04, CloseNode: opcode 0x05
    uint8_t open[] = { 0x04, 0x00, 0x00, 0x00 };
    uint8_t close[] = { 0x05, 0x00, 0x00, 0x00 };
    EXPECT_EQ(static_cast<GeoOpcode>(open[0]), GeoOpcode::OpenNode);
    EXPECT_EQ(static_cast<GeoOpcode>(close[0]), GeoOpcode::CloseNode);
}

TEST(SM64GeoLayoutTest, ByteLevelNodeTranslation) {
    // NodeTranslation (GeoOpcode::NodeTranslation = 17 = 0x11): 8 bytes
    // [0x11, drawLayer, x_hi, x_lo, y_hi, y_lo, z_hi, z_lo]
    // CMD_SIZE_SHIFT is 0, so offsets are direct byte offsets
    uint8_t cmd[] = {
        0x11,       // opcode (NodeTranslation = 17)
        0x01,       // drawingLayer
        0x00, 0x64, // x = 100 (big-endian)
        0xFF, 0x9C, // y = -100 (big-endian)
        0x01, 0xF4, // z = 500 (big-endian)
    };

    auto opcode = static_cast<GeoOpcode>(cmd[0]);
    EXPECT_EQ(opcode, GeoOpcode::NodeTranslation);

    // Read drawingLayer at offset 1 (same as cur_geo_cmd_u8(0x01))
    uint8_t drawingLayer = cmd[0x01];
    EXPECT_EQ(drawingLayer, 0x01);

    // Read int16_t values using BSWAP16 (same as cur_geo_cmd_s16)
    int16_t x = (int16_t)BSWAP16(*(int16_t*)&cmd[0x02]);
    int16_t y = (int16_t)BSWAP16(*(int16_t*)&cmd[0x04]);
    int16_t z = (int16_t)BSWAP16(*(int16_t*)&cmd[0x06]);
    EXPECT_EQ(x, 100);
    EXPECT_EQ(y, -100);
    EXPECT_EQ(z, 500);
}

TEST(SM64GeoLayoutTest, ByteLevelAssignAsView) {
    // AssignAsView (GeoOpcode::AssignAsView = 6): 4 bytes
    // [0x06, 0x00, idx_hi, idx_lo]
    uint8_t cmd[] = { 0x06, 0x00, 0x00, 0x0A };
    auto opcode = static_cast<GeoOpcode>(cmd[0]);
    EXPECT_EQ(opcode, GeoOpcode::AssignAsView);

    int16_t idx = (int16_t)BSWAP16(*(int16_t*)&cmd[0x02]);
    EXPECT_EQ(idx, 10);
}
