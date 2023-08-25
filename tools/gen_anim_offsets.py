#!/usr/bin/env python3
import os

# This script generates the offsets for the animation files.

OFFSET = 0x4EC690
HEADER_SIZE = 0x18
ANIMS_SIZE = 3

# struct Animation {
#     /*0x00*/ s16 flags;
#     /*0x02*/ s16 animYTransDivisor;
#     /*0x04*/ s16 startFrame;
#     /*0x06*/ s16 loopStart;
#     /*0x08*/ s16 loopEnd;
#     /*0x0A*/ s16 unusedBoneCount;
#     /*0x0C*/ const s16 *values;
#     /*0x10*/ const u16 *index;
#     /*0x14*/ u32 length; // only used with Mario animations to determine how much to load. 0 otherwise.
# };

# ./anim_01_02.inc.c./anim_07_08.inc.c./anim_0B_0C.inc.c./anim_0F_10.inc.c./anim_2C_2D.inc.c./anim_3C_3D.inc.c./anim_45_46.inc.c./anim_4D_4E.inc.c./anim_56_57.inc.c./anim_6F_70.inc.c./anim_72_73.inc.c./anim_88_89.inc.c./anim_8E_8F.inc.c./anim_B5_B6.inc.c./anim_BC_BD.inc.c./anim_CB_CC.inc.c%

multi = [0x01, 0x07, 0x0B, 0x0F, 0x2C, 0x3C, 0x45, 0x4D, 0x56, 0x6F, 0x72, 0x88, 0x8E, 0xB5, 0xBC, 0xCB]

if __name__ == "__main__":
    with open("baserom.us.z64", "rb") as f:
        f.seek(OFFSET)
        for i in range(ANIMS_SIZE):
            for j in range(2 if i in multi else 1):
                flags = int.from_bytes(f.read(2), byteorder="big")
                animYTransDivisor = int.from_bytes(f.read(2), byteorder="big")
                startFrame = int.from_bytes(f.read(2), byteorder="big")
                loopStart = int.from_bytes(f.read(2), byteorder="big")
                loopEnd = int.from_bytes(f.read(2), byteorder="big")
                unusedBoneCount = int.from_bytes(f.read(2), byteorder="big")
                values = int.from_bytes(f.read(4), byteorder="little")
                index = int.from_bytes(f.read(4), byteorder="little")
                length = int.from_bytes(f.read(2), byteorder="big")
                print(f"0x{OFFSET + i * HEADER_SIZE:08X}")
                print(f"    flags: {flags}")
                print(f"    animYTransDivisor: {animYTransDivisor}")
                print(f"    startFrame: {startFrame}")
                print(f"    loopStart: {loopStart}")
                print(f"    loopEnd: {loopEnd}")
                print(f"    unusedBoneCount: {unusedBoneCount}")
                print(f"    values: 0x{values:08X}")
                print(f"    index: 0x{index:08X}")
                print(f"    length: {length}")
                f.seek(HEADER_SIZE, os.SEEK_CUR)
            f.seek(0x18, os.SEEK_CUR)