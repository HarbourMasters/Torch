#include <gtest/gtest.h>
#include "factories/sm64/BehaviorScriptFactory.h"
#include <vector>

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
