#include <gtest/gtest.h>
#include "factories/sm64/BehaviorScriptFactory.h"
#include "lib/binarytools/endianness.h"
#include <vector>
#include <cstring>

using namespace SM64;

TEST(SM64BehaviorScriptTest, BehaviorCommandConstruction) {
    std::vector<BehaviorArgument> args;
    BehaviorCommand cmd(BehaviorOpcode::BEGIN, args);
    EXPECT_EQ(cmd.opcode, BehaviorOpcode::BEGIN);
    EXPECT_TRUE(cmd.arguments.empty());
}

TEST(SM64BehaviorScriptTest, BehaviorCommandWithArguments) {
    std::vector<BehaviorArgument> args;
    args.push_back((uint8_t)0x31);        // object list
    args.push_back((uint32_t)0x13000000); // behavior param
    BehaviorCommand cmd(BehaviorOpcode::BEGIN, args);
    ASSERT_EQ(cmd.arguments.size(), 2u);
    EXPECT_EQ(std::get<uint8_t>(cmd.arguments[0]), 0x31);
    EXPECT_EQ(std::get<uint32_t>(cmd.arguments[1]), 0x13000000u);
}

TEST(SM64BehaviorScriptTest, BehaviorScriptDataConstruction) {
    std::vector<BehaviorCommand> cmds;
    cmds.emplace_back(BehaviorOpcode::BEGIN, std::vector<BehaviorArgument>{(uint8_t)0x00});
    cmds.emplace_back(BehaviorOpcode::SET_INT, std::vector<BehaviorArgument>{(uint8_t)0x00, (int16_t)100});
    cmds.emplace_back(BehaviorOpcode::DEACTIVATE, std::vector<BehaviorArgument>{});

    BehaviorScriptData data(cmds);
    ASSERT_EQ(data.mCommands.size(), 3u);
    EXPECT_EQ(data.mCommands[0].opcode, BehaviorOpcode::BEGIN);
    EXPECT_EQ(data.mCommands[1].opcode, BehaviorOpcode::SET_INT);
    EXPECT_EQ(data.mCommands[2].opcode, BehaviorOpcode::DEACTIVATE);
}

TEST(SM64BehaviorScriptTest, HasExpectedExporters) {
    BehaviorScriptFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Code));
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}

TEST(SM64BehaviorScriptTest, BehaviorArgumentVariantTypes) {
    BehaviorArgument u8_arg = (uint8_t)42;
    BehaviorArgument s8_arg = (int8_t)-10;
    BehaviorArgument u16_arg = (uint16_t)1000;
    BehaviorArgument s16_arg = (int16_t)-500;
    BehaviorArgument u32_arg = (uint32_t)0xCAFEBABE;
    BehaviorArgument s32_arg = (int32_t)-100000;
    BehaviorArgument f32_arg = 3.14f;
    BehaviorArgument ptr_arg = (uint64_t)0x8033B000;

    EXPECT_EQ(std::get<uint8_t>(u8_arg), 42);
    EXPECT_EQ(std::get<int8_t>(s8_arg), -10);
    EXPECT_EQ(std::get<uint16_t>(u16_arg), 1000);
    EXPECT_EQ(std::get<int16_t>(s16_arg), -500);
    EXPECT_EQ(std::get<uint32_t>(u32_arg), 0xCAFEBABEu);
    EXPECT_EQ(std::get<int32_t>(s32_arg), -100000);
    EXPECT_FLOAT_EQ(std::get<float>(f32_arg), 3.14f);
    EXPECT_EQ(std::get<uint64_t>(ptr_arg), 0x8033B000u);
}

// Byte-level command parsing tests — verify binary format using the same
// BSWAP logic that cur_behavior_cmd_* macros use in BehaviorScriptFactory::parse

TEST(SM64BehaviorScriptTest, ByteLevelBeginCommand) {
    // BEGIN (opcode 0x00): 4 bytes
    // [0x00, objList, 0x00, 0x00]
    uint8_t cmd[] = { 0x00, 0x31, 0x00, 0x00 };

    auto opcode = static_cast<BehaviorOpcode>(cmd[0]);
    EXPECT_EQ(opcode, BehaviorOpcode::BEGIN);

    // cur_behavior_cmd_u8(0x01) reads object list
    uint8_t objList = cmd[0x01];
    EXPECT_EQ(objList, 0x31);
}

TEST(SM64BehaviorScriptTest, ByteLevelSetIntCommand) {
    // SET_INT (BehaviorOpcode::SET_INT = 16 = 0x10): 4 bytes
    // [0x10, field, value_hi, value_lo]
    uint8_t cmd[] = { 0x10, 0x1C, 0x00, 0x64 };

    auto opcode = static_cast<BehaviorOpcode>(cmd[0]);
    EXPECT_EQ(opcode, BehaviorOpcode::SET_INT);

    uint8_t field = cmd[0x01];
    int16_t value = (int16_t)BSWAP16(*(int16_t*)&cmd[0x02]);
    EXPECT_EQ(field, 0x1C);
    EXPECT_EQ(value, 100);
}

TEST(SM64BehaviorScriptTest, ByteLevelCallCommand) {
    // CALL (opcode 0x02): 8 bytes
    // [0x02, 0x00, 0x00, 0x00, addr3, addr2, addr1, addr0]
    uint8_t cmd[] = { 0x02, 0x00, 0x00, 0x00, 0x80, 0x33, 0xB0, 0x00 };

    auto opcode = static_cast<BehaviorOpcode>(cmd[0]);
    EXPECT_EQ(opcode, BehaviorOpcode::CALL);

    // cur_behavior_cmd_u32(0x04)
    uint32_t addr = BSWAP32(*(uint32_t*)&cmd[0x04]);
    EXPECT_EQ(addr, 0x8033B000u);
}
