#include <gtest/gtest.h>
#include "n64/CommandMacros.h"

// _SHIFTL: shift left and mask to width bits
TEST(CommandMacrosTest, ShiftLBasic) {
    // _SHIFTL(0xFF, 8, 8) = (0xFF & 0xFF) << 8 = 0xFF00
    EXPECT_EQ(_SHIFTL(0xFF, 8, 8), 0x0000FF00u);
}

TEST(CommandMacrosTest, ShiftLMask) {
    // _SHIFTL(0x1FF, 0, 8) = (0x1FF & 0xFF) << 0 = 0xFF (masks to 8 bits)
    EXPECT_EQ(_SHIFTL(0x1FF, 0, 8), 0xFFu);
}

TEST(CommandMacrosTest, ShiftLHighBits) {
    // _SHIFTL(0xDE, 24, 8) = 0xDE000000
    EXPECT_EQ(_SHIFTL(0xDE, 24, 8), 0xDE000000u);
}

TEST(CommandMacrosTest, ShiftLSixteenBit) {
    // _SHIFTL(0xABCD, 16, 16) = 0xABCD0000
    EXPECT_EQ(_SHIFTL(0xABCD, 16, 16), 0xABCD0000u);
}

// _SHIFTR: shift right and mask to width bits
TEST(CommandMacrosTest, ShiftRBasic) {
    // _SHIFTR(0xFF00, 8, 8) = (0xFF00 >> 8) & 0xFF = 0xFF
    EXPECT_EQ(_SHIFTR(0xFF00, 8, 8), 0xFFu);
}

TEST(CommandMacrosTest, ShiftRHighByte) {
    // _SHIFTR(0xDE000000, 24, 8) = 0xDE
    EXPECT_EQ(_SHIFTR(0xDE000000, 24, 8), 0xDEu);
}

// CMD_SIZE_SHIFT: sizeof(uint32_t) >> 3 = 4 >> 3 = 0
TEST(CommandMacrosTest, CmdSizeShift) {
    EXPECT_EQ(CMD_SIZE_SHIFT, 0u);
}

// CMD_PROCESS_OFFSET: with CMD_SIZE_SHIFT=0, should be identity
TEST(CommandMacrosTest, CmdProcessOffsetIdentity) {
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(CMD_PROCESS_OFFSET(i), (uint32_t)i) << "offset " << i;
    }
}

// CMD_BBH: pack 2 bytes + 1 halfword
TEST(CommandMacrosTest, CmdBBH) {
    // CMD_BBH(0x00, 0x19, 0x0004)
    // = _SHIFTL(0x00,0,8) | _SHIFTL(0x19,8,8) | _SHIFTL(0x0004,16,16)
    // = 0x00 | 0x1900 | 0x00040000
    // = 0x00041900
    EXPECT_EQ(CMD_BBH(0x00, 0x19, 0x0004), 0x00041900u);
}

// CMD_HH: pack 2 halfwords
TEST(CommandMacrosTest, CmdHH) {
    // CMD_HH(0x1234, 0x5678) = 0x56781234
    EXPECT_EQ(CMD_HH(0x1234, 0x5678), 0x56781234u);
}

// CMD_W: identity
TEST(CommandMacrosTest, CmdW) {
    EXPECT_EQ(CMD_W(0xDEADBEEF), 0xDEADBEEFu);
}
