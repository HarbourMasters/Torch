#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <fcntl.h>
#endif

#include "displaylist_unpack.h"
#include "mk_gbi.h"

// Avoid compiler warnings for unused variables
#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#define _SHIFTL(v, s, w)	\
    ((unsigned int) (((unsigned int)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)	\
    ((unsigned int)(((unsigned int)(v) >> (s)) & ((0x01 << (w)) - 1)))

#define _SHIFT _SHIFTL	/* old, for compatibility only */

uint32_t sGfxSeekPosition;
uint32_t sPackedSeekPosition;
uint32_t gIsMirrorMode = 0;
#define SEGMENT_ADDR(num, off) (((num) << 24) + (off))

void unpack_lights(Gfx *arg0, UNUSED const uint8_t *arg1, int8_t arg2) {
    int32_t a = (arg2 * 0x18) + 0x9000008;
    int32_t b = (arg2 * 0x18) + 0x9000000;
    Gfx macro[] = {gsSPNumLights(1)};

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;

    sGfxSeekPosition++;
    arg0[sGfxSeekPosition].words.w0 = 0x3860010;

    arg0[sGfxSeekPosition].words.w1 = a;

    sGfxSeekPosition++;
    arg0[sGfxSeekPosition].words.w0 = 0x3880010;
    arg0[sGfxSeekPosition].words.w1 = b;
    sGfxSeekPosition++;
}

void unpack_displaylist(Gfx *arg0, const uint8_t *args, UNUSED int8_t opcode) {
    uint32_t temp_v0 = args[sPackedSeekPosition++];
    uintptr_t temp_t7 = ((args[sPackedSeekPosition++]) << 8 | temp_v0) * 8;
    arg0[sGfxSeekPosition].words.w0 = 0x06000000;
    // Segment seven addr
    arg0[sGfxSeekPosition].words.w1 = 0x07000000 + temp_t7;
    sGfxSeekPosition++;
}

// end displaylist
void unpack_end_displaylist(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED int8_t arg2) {
    arg0[sGfxSeekPosition].words.w0 = G_ENDDL << 24;
    arg0[sGfxSeekPosition].words.w1 = 0;
    sGfxSeekPosition++;
}

void unpack_set_geometry_mode(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED int8_t arg2) {
    Gfx macro[] = {gsSPSetGeometryMode(G_CULL_BACK)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_clear_geometry_mode(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED int8_t arg2) {
    Gfx macro[] = {gsSPClearGeometryMode(G_CULL_BACK)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_cull_displaylist(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED int8_t arg2) {
    Gfx macro[] = {gsSPCullDisplayList(0, 7)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode1(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_MODULATERGBA)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode2(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetCombineMode(G_CC_MODULATERGBDECALA, G_CC_MODULATERGBDECALA)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode_shade(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode4(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetCombineMode(G_CC_MODULATERGBDECALA, G_CC_MODULATERGBDECALA)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode5(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetCombineMode(G_CC_DECALRGBA, G_CC_DECALRGBA)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_opaque(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_tex_edge(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetRenderMode(G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_translucent(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_opaque_decal(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetRenderMode(G_RM_AA_ZB_OPA_DECAL, G_RM_AA_ZB_OPA_DECAL)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_translucent_decal(Gfx *arg0, UNUSED const uint8_t *arg1, UNUSED uint32_t arg2) {
    Gfx macro[] = {gsDPSetRenderMode(G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL)};
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_tile_sync(Gfx *gfx,const uint8_t *args, int8_t opcode) {
    Gfx tileSync[] = { gsDPTileSync() };
    uint32_t temp_a0;
    uint32_t lo;
    uint32_t hi;

    int32_t width;
    int32_t height;
    int32_t fmt;
    int32_t siz;
    int32_t line;
    int32_t tmem;
    int32_t cms;
    int32_t masks;
    int32_t cmt;
    int32_t maskt;
    int32_t lrs;
    int32_t lrt;

    tmem = 0;
    switch (opcode) {
        case 26:
            width = 32;
            height = 32;
            fmt = 0;
            break;
        case 44:
            width = 32;
            height = 32;
            fmt = 0;
            tmem = 256;
            break;
        case 27:
            width = 64;
            height = 32;
            fmt = 0;
            break;
        case 28:
            width = 32;
            height = 64;
            fmt = 0;
            break;
        case 29:
            width = 32;
            height = 32;
            fmt = 3;
            break;
        case 30:
            width = 64;
            height = 32;
            fmt = 3;
            break;
        case 31:
            width = 32;
            height = 64;
            fmt = 3;
            break;
    }

    // Set arguments

    siz = G_IM_SIZ_16b_BYTES;
    line = ((((width * 2) + 7) >> 3));

    temp_a0 = args[sPackedSeekPosition++];
    cms = temp_a0 & 0xF;
    masks = (temp_a0 & 0xF0) >> 4;

    temp_a0 = args[sPackedSeekPosition++];
    cmt = temp_a0 & 0xF;
    maskt = (temp_a0 & 0xF0) >> 4;

    // Generate gfx

    gfx[sGfxSeekPosition].words.w0 = tileSync->words.w0;
    gfx[sGfxSeekPosition].words.w1 = tileSync->words.w1;
    sGfxSeekPosition++;

    lo = (G_SETTILE << 24) | (fmt << 21) | (siz << 19) | (line << 9) | tmem;
    hi = ((cmt) << 18) | ((maskt) << 14) | ((cms) << 8) | ((masks) << 4);
    
    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = hi;
    sGfxSeekPosition++;

    lrs = (width - 1) << 2;
    lrt = (height - 1) << 2;

    lo = (G_SETTILESIZE << 24);
    hi = (lrs << 12) | lrt;

    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = hi;
    sGfxSeekPosition++;
}

void unpack_tile_load_sync(Gfx *gfx, const uint8_t *args, int8_t opcode) {
    UNUSED uint32_t var;
    Gfx tileSync[] = { gsDPTileSync() };
    Gfx loadSync[] = { gsDPLoadSync() };

    uint32_t arg;
    uint32_t lo;
    uint32_t hi;
    uint32_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t fmt;
    uint32_t siz;
    uint32_t tmem;
    uint32_t tile;

    switch (opcode) {
        case 32:
            width = 32;
            height = 32;
            fmt = 0;
            break;
        case 33:
            width = 64;
            height = 32;
            fmt = 0;
            break;
        case 34:
            width = 32;
            height = 64;
            fmt = 0;
            break;
        case 35:
            width = 32;
            height = 32;
            fmt = 3;
            break;
        case 36:
            width = 64;
            height = 32;
            fmt = 3;
            break;
        case 37:
            width = 32;
            height = 64;
            fmt = 3;
            break;
    }

    // Set arguments

    // Waa?
    var = args[sPackedSeekPosition];
    // Generates a texture address.
    addr = SEGMENT_ADDR(0x05, args[sPackedSeekPosition++] << 11);
    sPackedSeekPosition++;
    arg = args[sPackedSeekPosition++];
    siz = G_IM_SIZ_16b;
    tmem = (arg & 0xF);
    tile = (arg & 0xF0) >> 4;

    // Generate gfx

    lo = (G_SETTIMG << 24) | (fmt << 21) | (siz << 19);
    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = addr;
    sGfxSeekPosition++;

    gfx[sGfxSeekPosition].words.w0 = tileSync->words.w0;
    gfx[sGfxSeekPosition].words.w1 = tileSync->words.w1;
    sGfxSeekPosition++;

    lo = (G_SETTILE << 24) | (fmt << 21) | (siz << 19) | tmem;
    hi = tile << 24;

    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = hi;
    sGfxSeekPosition++;

    gfx[sGfxSeekPosition].words.w0 = loadSync->words.w0;
    gfx[sGfxSeekPosition].words.w1 = loadSync->words.w1;
    sGfxSeekPosition++;

    lo = G_LOADBLOCK << 24;
    hi = (tile << 24) | (MIN((width * height) - 1, 0x7FF) << 12) | CALC_DXT(width, G_IM_SIZ_16b_BYTES);

    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = hi;
    sGfxSeekPosition++;
}

void unpack_texture_on(Gfx *arg0, UNUSED const uint8_t *args, UNUSED int8_t arg2) {
    Gfx macro[] = { gsSPTexture(0xFFFF, 0xFFFF, 0, 0, 1) };
    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_texture_off(Gfx *arg0, UNUSED const uint8_t *args, UNUSED int8_t arg2) {
    Gfx macro[] = { gsSPTexture(0x1, 0x1, 0, 0, 0) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_vtx1(Gfx *gfx, const uint8_t *args, UNUSED int8_t arg2) {
    uint32_t temp_t7;
    uint32_t temp_t7_2;

    uint32_t temp = args[sPackedSeekPosition++];
    uint32_t temp2 = ((args[sPackedSeekPosition++] << 8) | temp) * 0x10;

    temp = args[sPackedSeekPosition++];
    temp_t7 = temp & 0x3F;
    temp = args[sPackedSeekPosition++];
    temp_t7_2 = temp & 0x3F;

    gfx[sGfxSeekPosition].words.w0 = (G_VTX << 24) | (temp_t7_2 * 2 << 16) | (((temp_t7 << 10) + ((0x10 * temp_t7) - 1)));
    gfx[sGfxSeekPosition].words.w1 = 0x04000000 + temp2;
    sGfxSeekPosition++;
}

void unpack_vtx2(Gfx *gfx, const uint8_t *args, int8_t arg2) {
    uint32_t temp_t9;
    uint32_t temp_v1;
    uint32_t temp_v2;

    temp_v1 = args[sPackedSeekPosition++];
    temp_v2 = ((args[sPackedSeekPosition++] << 8) | temp_v1) * 0x10;

    temp_t9 = arg2 - 50;

    gfx[sGfxSeekPosition].words.w0 = (G_VTX << 24) | ((temp_t9 << 10) + (((temp_t9) * 0x10) - 1));
    gfx[sGfxSeekPosition].words.w1 = 0x4000000 + temp_v2;
    sGfxSeekPosition++;
}

void unpack_triangle(Gfx *gfx, const uint8_t *args, UNUSED int8_t arg2) {
    uint32_t temp_v0;
    uint32_t phi_a0;
    uint32_t phi_a2;
    uint32_t phi_a3;

    temp_v0 = args[sPackedSeekPosition++];

    if (gIsMirrorMode) {
        phi_a3 = temp_v0 & 0x1F;
        phi_a2 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_a2 |= (temp_v0 & 3) * 8;
        phi_a0 = (temp_v0 >> 2) & 0x1F;
    } else {
        phi_a0 = temp_v0 & 0x1F;
        phi_a2 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_a2 |= (temp_v0 & 3) * 8;
        phi_a3 = (temp_v0 >> 2) & 0x1F;
    }
    gfx[sGfxSeekPosition].words.w0 = (G_TRI1 << 24);
    gfx[sGfxSeekPosition].words.w1 = ((phi_a0 * 2) << 16) | ((phi_a2 * 2) << 8) | (phi_a3 * 2);
    sGfxSeekPosition++;
}

void unpack_quadrangle(Gfx *gfx, const uint8_t *args, UNUSED int8_t arg2) {
    uint32_t temp_v0;
    uint32_t phi_t0;
    uint32_t phi_a3;
    uint32_t phi_a0;
    uint32_t phi_t2;
    uint32_t phi_t1;
    uint32_t phi_a2;

    temp_v0 = args[sPackedSeekPosition++];

    if (gIsMirrorMode) {
        phi_t0 = temp_v0 & 0x1F;
        phi_a3 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_a3 |= (temp_v0 & 3) * 8;
        phi_a0 = (temp_v0 >> 2) & 0x1F;
    } else {
        phi_a0 = temp_v0 & 0x1F;
        phi_a3 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_a3 |= (temp_v0 & 3) * 8;
        phi_t0 = (temp_v0 >> 2) & 0x1F;
    }

    temp_v0 = args[sPackedSeekPosition++];

    if (gIsMirrorMode) {
        phi_a2 = temp_v0 & 0x1F;
        phi_t1 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_t1 |= (temp_v0 & 3) * 8;
        phi_t2 = (temp_v0 >> 2) & 0x1F;
    } else {
        phi_t2 = temp_v0 & 0x1F;
        phi_t1 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_t1 |= (temp_v0 & 3) * 8;
        phi_a2 = (temp_v0 >> 2) & 0x1F;
    }
    gfx[sGfxSeekPosition].words.w0 =
        (G_TRI2 << 24) | ((phi_a0 * 2) << 16) | ((phi_a3 * 2) << 8) | (phi_t0 * 2);
    gfx[sGfxSeekPosition].words.w1 = ((phi_t2 * 2) << 16) | ((phi_t1 * 2) << 8) | (phi_a2 * 2);
    sGfxSeekPosition++;
}

void unpack_spline_3D(Gfx *gfx, const uint8_t *arg1, UNUSED int8_t arg2) {
    uint32_t temp_v0;
    uint32_t phi_a0;
    uint32_t phi_t0;
    uint32_t phi_a3;
    uint32_t phi_a2;

    temp_v0 = arg1[sPackedSeekPosition++];

    if (gIsMirrorMode != 0) {
        phi_a0 = temp_v0 & 0x1F;
        phi_a2 = ((temp_v0 >> 5) & 7);
        temp_v0 = arg1[sPackedSeekPosition++];
        phi_a2 |= ((temp_v0 & 3) * 8);
        phi_a3 = (temp_v0 >> 2) & 0x1F;
        phi_t0 = ((temp_v0 >> 7) & 1);
        temp_v0 = arg1[sPackedSeekPosition++];
        phi_t0 |= (temp_v0 & 0xF) * 2;
    } else {
        phi_t0 = temp_v0 & 0x1F;
        phi_a3 = ((temp_v0 >> 5) & 7);
        temp_v0 = arg1[sPackedSeekPosition++];
        phi_a3 |= ((temp_v0 & 3) * 8);
        phi_a2 = (temp_v0 >> 2) & 0x1F;
        phi_a0 = ((temp_v0 >> 7) & 1);
        temp_v0 = arg1[sPackedSeekPosition++];
        phi_a0 |= (temp_v0 & 0xF) * 2;
    }
    gfx[sGfxSeekPosition].words.w0 = (G_LINE3D << 24);
    gfx[sGfxSeekPosition].words.w1 =
        ((phi_a0 * 2) << 24) | ((phi_t0 * 2) << 16) | ((phi_a3 * 2) << 8) | (phi_a2 * 2);
    sGfxSeekPosition++;
}

/**
 * Unpacks course packed displaylists by iterating through each byte of the packed file.
 * Each packed displaylist entry has an opcode and any number of arguments.
 * The opcodes range from 0 to 87 which are used to run the relevant unpack function.
 * The file pointer increments when arguments are used. This way,
 * displaylist_unpack will always read an opcode and not an argument by accident.
 * 
 * @warning opcodes that do not contain a definition in the switch are ignored. If an undefined opcode
 * contained arguments the unpacker might try to unpack those arguments.
 * This issue is prevented so long as the packed file adheres to correct opcodes and unpack code
 * increments the file pointer the correct number of times.
 */

/* packed_dl ptr to compressed data, gfx ptr to decompressed data */
void displaylist_unpack(const uint8_t *packed_dl, Gfx *gfx, uint32_t arg2) {
    uint8_t opcode;

    sGfxSeekPosition = 0;
    sPackedSeekPosition = 0;

size_t i = 0;
    while(true) {
i++;
if (i > 10) {break;}
        // Seek to the next byte
        opcode = packed_dl[sPackedSeekPosition++];

        // Break when the eof has been reached denoted by opcode 0xFF
        if (opcode == 0xFF) break;
        
        switch (opcode) {
            case 0x0:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x1:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x2:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x3:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x4:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x5:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x6:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x7:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x8:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x9:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0xA:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0xB:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0xC:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0xD:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0xE:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0xF:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x10:
                unpack_lights(gfx, packed_dl, opcode);
                break; 
            case 0x11:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x12:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x13:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x14:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x15:
                unpack_combine_mode1(gfx, packed_dl, arg2);
                break;
            case 0x16:
                unpack_combine_mode2(gfx, packed_dl, arg2);
                break;
            case 0x17:
                unpack_combine_mode_shade(gfx, packed_dl, arg2);
                break;
            case 0x2E:
                unpack_combine_mode4(gfx, packed_dl, arg2);
                break;
            case 0x53:
                unpack_combine_mode5(gfx, packed_dl, arg2);
                break;
            case 0x18:
                unpack_render_mode_opaque(gfx, packed_dl, arg2);
                break;
            case 0x19:
                unpack_render_mode_tex_edge(gfx, packed_dl, arg2);
                break;
            case 0x2F:
                unpack_render_mode_translucent(gfx, packed_dl, arg2);
                break;
            case 0x54:
                unpack_render_mode_opaque_decal(gfx, packed_dl, arg2);
                break;
            case 0x55:
                unpack_render_mode_translucent_decal(gfx, packed_dl, arg2);
                break;
            case 0x1A:
                unpack_tile_sync(gfx, packed_dl, opcode);
                break;
            case 0x2C:
                unpack_tile_sync(gfx, packed_dl, opcode);
                break;
            case 0x1B:
                unpack_tile_sync(gfx, packed_dl, opcode);
                break;
            case 0x1C:
                unpack_tile_sync(gfx, packed_dl, opcode);
                break;
            case 0x1D:
                unpack_tile_sync(gfx, packed_dl, opcode);
                break;
            case 0x1E:
                unpack_tile_sync(gfx, packed_dl, opcode);
                break;
            case 0x1F:
                unpack_tile_sync(gfx, packed_dl, opcode);
                break;
            case 0x20:
                unpack_tile_load_sync(gfx, packed_dl, opcode);
                break;
            case 0x21:
                unpack_tile_load_sync(gfx, packed_dl, opcode);
                break;
            case 0x22:
                unpack_tile_load_sync(gfx, packed_dl, opcode);
                break;
            case 0x23:
                unpack_tile_load_sync(gfx, packed_dl, opcode);
                break;
            case 0x24:
                unpack_tile_load_sync(gfx, packed_dl, opcode);
                break;
            case 0x25:
                unpack_tile_load_sync(gfx, packed_dl, opcode);
                break;
            case 0x26:
                unpack_texture_on(gfx, packed_dl, opcode);
                break;
            case 0x27:
                unpack_texture_off(gfx, packed_dl, opcode);
                break;
            case 0x28:
                unpack_vtx1(gfx, packed_dl, opcode);
                break;
            case 0x33:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x34:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x35:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x36:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x37:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x38:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x39:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x3A:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x3B:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x3C:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x3D:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x3E:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x3F:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x40:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x41:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x42:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x43:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x44:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x45:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x46:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x47:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x48:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x49:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x4A:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x4B:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x4C:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x4D:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x4E:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x4F:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x50:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x51:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x52:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x29:
                unpack_triangle(gfx, packed_dl, opcode);
                break;
            case 0x58:
                unpack_quadrangle(gfx, packed_dl, opcode);
                break;
            case 0x30:
                unpack_spline_3D(gfx, packed_dl, opcode);
                break;
            case 0x2D:
                unpack_cull_displaylist(gfx, packed_dl, opcode);
                break;
            case 0x2A:
                unpack_end_displaylist(gfx, packed_dl, opcode);
                break;
            case 0x56:
                unpack_set_geometry_mode(gfx, packed_dl, opcode);
                break;
            case 0x57:
                unpack_clear_geometry_mode(gfx, packed_dl, opcode);
                break;
            case 0x2B:
                unpack_displaylist(gfx, packed_dl, opcode);
                break;
            default:
                // Skip unknown values
                break;
        }
    }
}