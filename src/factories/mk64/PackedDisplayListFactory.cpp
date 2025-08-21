#include "PackedDisplayListFactory.h"
#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "../DisplayListOverrides.h" // pour N64Gfx
#include <string>

#ifdef STANDALONE
#include <gfxd.h>
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define G_SPNOOP 0
#define G_MTX 1
#define G_RESERVED0 2
#define G_MOVEMEM 3
#define G_VTX 4
#define G_RESERVED1 5
#define G_DL 6
#define G_RESERVED2 7
#define G_RESERVED3 8
#define G_SPRITE2D_BASE 9

#define G_MV_VIEWPORT 0x80
#define G_MV_LOOKATY 0x82
#define G_MV_LOOKATX 0x84
#define G_MV_L0 0x86
#define G_MV_L1 0x88
#define G_MV_L2 0x8a
#define G_MV_L3 0x8c
#define G_MV_L4 0x8e
#define G_MV_L5 0x90
#define G_MV_L6 0x92
#define G_MV_L7 0x94
#define G_MV_TXTATT 0x96
#define G_MV_MATRIX_1 0x9e
#define G_MV_MATRIX_2 0x98
#define G_MV_MATRIX_3 0x9a
#define G_MV_MATRIX_4 0x9c

#define G_SPNOOP 0
#define G_MTX 1
#define G_RESERVED0 2
#define G_MOVEMEM 3
#define G_VTX 4
#define G_RESERVED1 5
#define G_DL 6
#define G_RESERVED2 7
#define G_RESERVED3 8
#define G_SPRITE2D_BASE 9

#define G_IMMFIRST -65
#define G_TRI1 (G_IMMFIRST - 0)
#define G_CULLDL (G_IMMFIRST - 1)
#define G_POPMTX (G_IMMFIRST - 2)
#define G_MOVEWORD (G_IMMFIRST - 3)
#define G_TEXTURE (G_IMMFIRST - 4)
#define G_SETOTHERMODE_H (G_IMMFIRST - 5)
#define G_SETOTHERMODE_L (G_IMMFIRST - 6)
#define G_ENDDL (G_IMMFIRST - 7)
#define G_SETGEOMETRYMODE (G_IMMFIRST - 8)
#define G_CLEARGEOMETRYMODE (G_IMMFIRST - 9)
#define G_LINE3D (G_IMMFIRST - 10)
#define G_RDPHALF_1 (G_IMMFIRST - 11)
#define G_RDPHALF_2 (G_IMMFIRST - 12)
#define G_MODIFYVTX (G_IMMFIRST - 13)
#define G_TRI2 (G_IMMFIRST - 14)
#define G_BRANCH_Z (G_IMMFIRST - 15)
#define G_LOAD_UCODE (G_IMMFIRST - 16)
#define G_QUAD (G_IMMFIRST - 10)

#define G_CC_PRIMITIVE 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE
#define G_CC_SHADE 0, 0, 0, SHADE, 0, 0, 0, SHADE
#define G_CC_MODULATEI TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE
#define G_CC_MODULATEIA TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0
#define G_CC_MODULATEIDECALA TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0
#define G_CC_MODULATERGB G_CC_MODULATEI
#define G_CC_MODULATERGBA G_CC_MODULATEIA
#define G_CC_MODULATERGBDECALA G_CC_MODULATEIDECALA
#define G_CC_MODULATEI_PRIM TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE
#define G_CC_MODULATEIA_PRIM TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0
#define G_CC_MODULATEIDECALA_PRIM TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, TEXEL0
#define G_CC_MODULATERGB_PRIM G_CC_MODULATEI_PRIM
#define G_CC_MODULATERGBA_PRIM G_CC_MODULATEIA_PRIM
#define G_CC_MODULATERGBDECALA_PRIM G_CC_MODULATEIDECALA_PRIM
#define G_CC_DECALRGB 0, 0, 0, TEXEL0, 0, 0, 0, SHADE
#define G_CC_DECALRGBA 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_BLENDI ENVIRONMENT, SHADE, TEXEL0, SHADE, 0, 0, 0, SHADE
#define G_CC_BLENDIA ENVIRONMENT, SHADE, TEXEL0, SHADE, TEXEL0, 0, SHADE, 0
#define G_CC_BLENDIDECALA ENVIRONMENT, SHADE, TEXEL0, SHADE, 0, 0, 0, TEXEL0
#define G_CC_BLENDRGBA TEXEL0, SHADE, TEXEL0_ALPHA, SHADE, 0, 0, 0, SHADE
#define G_CC_BLENDRGBDECALA TEXEL0, SHADE, TEXEL0_ALPHA, SHADE, 0, 0, 0, TEXEL0
#define G_CC_ADDRGB 1, 0, TEXEL0, SHADE, 0, 0, 0, SHADE
#define G_CC_ADDRGBDECALA 1, 0, TEXEL0, SHADE, 0, 0, 0, TEXEL0
#define G_CC_REFLECTRGB ENVIRONMENT, 0, TEXEL0, SHADE, 0, 0, 0, SHADE
#define G_CC_REFLECTRGBDECALA ENVIRONMENT, 0, TEXEL0, SHADE, 0, 0, 0, TEXEL0
#define G_CC_HILITERGB PRIMITIVE, SHADE, TEXEL0, SHADE, 0, 0, 0, SHADE
#define G_CC_HILITERGBA PRIMITIVE, SHADE, TEXEL0, SHADE, PRIMITIVE, SHADE, TEXEL0, SHADE
#define G_CC_HILITERGBDECALA PRIMITIVE, SHADE, TEXEL0, SHADE, 0, 0, 0, TEXEL0
#define G_CC_SHADEDECALA 0, 0, 0, SHADE, 0, 0, 0, TEXEL0
#define G_CC_BLENDPE PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, SHADE, 0
#define G_CC_BLENDPEDECALA PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, TEXEL0

#define G_IM_SIZ_16b_BYTES 2

#define G_SETCIMG 0xff         /*  -1 */
#define G_SETZIMG 0xfe         /*  -2 */
#define G_SETTIMG 0xfd         /*  -3 */
#define G_SETCOMBINE 0xfc      /*  -4 */
#define G_SETENVCOLOR 0xfb     /*  -5 */
#define G_SETPRIMCOLOR 0xfa    /*  -6 */
#define G_SETBLENDCOLOR 0xf9   /*  -7 */
#define G_SETFOGCOLOR 0xf8     /*  -8 */
#define G_SETFILLCOLOR 0xf7    /*  -9 */
#define G_FILLRECT 0xf6        /* -10 */
#define G_SETTILE 0xf5         /* -11 */
#define G_LOADTILE 0xf4        /* -12 */
#define G_LOADBLOCK 0xf3       /* -13 */
#define G_SETTILESIZE 0xf2     /* -14 */
#define G_LOADTLUT 0xf0        /* -16 */
#define G_RDPSETOTHERMODE 0xef /* -17 */
#define G_SETPRIMDEPTH 0xee    /* -18 */
#define G_SETSCISSOR 0xed      /* -19 */
#define G_SETCONVERT 0xec      /* -20 */
#define G_SETKEYR 0xeb         /* -21 */
#define G_SETKEYGB 0xea        /* -22 */
#define G_RDPFULLSYNC 0xe9     /* -23 */
#define G_RDPTILESYNC 0xe8     /* -24 */
#define G_RDPPIPESYNC 0xe7     /* -25 */
#define G_RDPLOADSYNC 0xe6     /* -26 */
#define G_TEXRECTFLIP 0xe5     /* -27 */
#define G_TEXRECT 0xe4         /* -28 */

#define gsDPNoParam(cmd)           \
    {                              \
        { _SHIFTL(cmd, 24, 8), 0 } \
    }

#define G_CCMUX_COMBINED 0
#define G_CCMUX_TEXEL0 1
#define G_CCMUX_TEXEL1 2
#define G_CCMUX_PRIMITIVE 3
#define G_CCMUX_SHADE 4
#define G_CCMUX_ENVIRONMENT 5
#define G_CCMUX_CENTER 6
#define G_CCMUX_SCALE 6
#define G_CCMUX_COMBINED_ALPHA 7
#define G_CCMUX_TEXEL0_ALPHA 8
#define G_CCMUX_TEXEL1_ALPHA 9
#define G_CCMUX_PRIMITIVE_ALPHA 10
#define G_CCMUX_SHADE_ALPHA 11
#define G_CCMUX_ENV_ALPHA 12
#define G_CCMUX_LOD_FRACTION 13
#define G_CCMUX_PRIM_LOD_FRAC 14
#define G_CCMUX_NOISE 7
#define G_CCMUX_K4 7
#define G_CCMUX_K5 15
#define G_CCMUX_1 6
#define G_CCMUX_0 31

#define G_ACMUX_COMBINED 0
#define G_ACMUX_TEXEL0 1
#define G_ACMUX_TEXEL1 2
#define G_ACMUX_PRIMITIVE 3
#define G_ACMUX_SHADE 4
#define G_ACMUX_ENVIRONMENT 5
#define G_ACMUX_LOD_FRACTION 0
#define G_ACMUX_PRIM_LOD_FRAC 6
#define G_ACMUX_1 6
#define G_ACMUX_0 7

#define gsDPTileSync() gsDPNoParam(G_RDPTILESYNC)
#define gsDPLoadSync() gsDPNoParam(G_RDPLOADSYNC)

#define G_IM_SIZ_16b 2

#define G_TX_RENDERTILE 0

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define G_TX_DXT_FRAC 11

#define TXL2WORDS(txls, b_txl) MAX(1, ((txls) * (b_txl) / 8))
#define CALC_DXT(width, b_txl) (((1 << G_TX_DXT_FRAC) + TXL2WORDS(width, b_txl) - 1) / TXL2WORDS(width, b_txl))

#define G_CC_MODULATERGBA G_CC_MODULATEIA

#define BOWTIE_VAL 0

#define gsSPTexture(s, t, level, tile, on)                                                                          \
    {                                                                                                               \
        (_SHIFTL(G_TEXTURE, 24, 8) | _SHIFTL(BOWTIE_VAL, 16, 8) | _SHIFTL((level), 11, 3) | _SHIFTL((tile), 8, 3) | \
         _SHIFTL((on), 0, 8)),                                                                                      \
            (_SHIFTL((s), 16, 16) | _SHIFTL((t), 0, 16))                                                            \
    }

#define gsDPSetCombineLERP(a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0, a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1)                     \
    {                                                                                                                  \
        {                                                                                                              \
            _SHIFTL(G_SETCOMBINE, 24, 8) | _SHIFTL(GCCc0w0(G_CCMUX_##a0, G_CCMUX_##c0, G_ACMUX_##Aa0, G_ACMUX_##Ac0) | \
                                                       GCCc1w0(G_CCMUX_##a1, G_CCMUX_##c1),                            \
                                                   0, 24),                                                             \
                (unsigned int) (GCCc0w1(G_CCMUX_##b0, G_CCMUX_##d0, G_ACMUX_##Ab0, G_ACMUX_##Ad0) |                    \
                                GCCc1w1(G_CCMUX_##b1, G_ACMUX_##Aa1, G_ACMUX_##Ac1, G_CCMUX_##d1, G_ACMUX_##Ab1,       \
                                        G_ACMUX_##Ad1))                                                                \
        }                                                                                                              \
    }

#define GCCc0w0(saRGB0, mRGB0, saA0, mA0) \
    (_SHIFTL((saRGB0), 20, 4) | _SHIFTL((mRGB0), 15, 5) | _SHIFTL((saA0), 12, 3) | _SHIFTL((mA0), 9, 3))

#define GCCc1w0(saRGB1, mRGB1) (_SHIFTL((saRGB1), 5, 4) | _SHIFTL((mRGB1), 0, 5))

#define GCCc0w1(sbRGB0, aRGB0, sbA0, aA0) \
    (_SHIFTL((sbRGB0), 28, 4) | _SHIFTL((aRGB0), 15, 3) | _SHIFTL((sbA0), 12, 3) | _SHIFTL((aA0), 9, 3))

#define GCCc1w1(sbRGB1, saA1, mA1, aRGB1, sbA1, aA1)                                                      \
    (_SHIFTL((sbRGB1), 24, 4) | _SHIFTL((saA1), 21, 3) | _SHIFTL((mA1), 18, 3) | _SHIFTL((aRGB1), 6, 3) | \
     _SHIFTL((sbA1), 3, 3) | _SHIFTL((aA1), 0, 3))

#define gsDPSetCombineMode(a, b) gsDPSetCombineLERP(a, b)

#define gsSPSetOtherMode(cmd, sft, len, data)                                                    \
    {                                                                                            \
        { _SHIFTL(cmd, 24, 8) | _SHIFTL(sft, 8, 8) | _SHIFTL(len, 0, 8), (unsigned int) (data) } \
    }

#define gsDPSetRenderMode(c0, c1) gsSPSetOtherMode(G_SETOTHERMODE_L, G_MDSFT_RENDERMODE, 29, (c0) | (c1))

#define G_MDSFT_ALPHACOMPARE 0
#define G_MDSFT_ZSRCSEL 2
#define G_MDSFT_RENDERMODE 3
#define G_MDSFT_BLENDER 16

#define AA_EN 0x8
#define Z_CMP 0x10
#define Z_UPD 0x20
#define IM_RD 0x40
#define CLR_ON_CVG 0x80
#define CVG_DST_CLAMP 0
#define CVG_DST_WRAP 0x100
#define CVG_DST_FULL 0x200
#define CVG_DST_SAVE 0x300
#define ZMODE_OPA 0
#define ZMODE_INTER 0x400
#define ZMODE_XLU 0x800
#define ZMODE_DEC 0xc00
#define CVG_X_ALPHA 0x1000
#define ALPHA_CVG_SEL 0x2000
#define FORCE_BL 0x4000
#define TEX_EDGE 0x0000 /* used to be 0x8000 */

#define G_BL_CLR_IN 0
#define G_BL_CLR_MEM 1
#define G_BL_CLR_BL 2
#define G_BL_CLR_FOG 3
#define G_BL_1MA 0
#define G_BL_A_MEM 1
#define G_BL_A_IN 0
#define G_BL_A_FOG 1
#define G_BL_A_SHADE 2
#define G_BL_1 2
#define G_BL_0 3

#define GBL_c1(m1a, m1b, m2a, m2b) (m1a) << 30 | (m1b) << 26 | (m2a) << 22 | (m2b) << 18
#define GBL_c2(m1a, m1b, m2a, m2b) (m1a) << 28 | (m1b) << 24 | (m2a) << 20 | (m2b) << 16

#define RM_AA_ZB_OPA_SURF(clk)                                                  \
    AA_EN | Z_CMP | Z_UPD | IM_RD | CVG_DST_CLAMP | ZMODE_OPA | ALPHA_CVG_SEL | \
        GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_A_MEM)

#define RM_AA_ZB_TEX_EDGE(clk)                                                                           \
    AA_EN | Z_CMP | Z_UPD | IM_RD | CVG_DST_CLAMP | CVG_X_ALPHA | ALPHA_CVG_SEL | ZMODE_OPA | TEX_EDGE | \
        GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_A_MEM)

#define RM_AA_ZB_XLU_SURF(clk)                                                 \
    AA_EN | Z_CMP | IM_RD | CVG_DST_WRAP | CLR_ON_CVG | FORCE_BL | ZMODE_XLU | \
        GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA)

#define RM_AA_ZB_OPA_DECAL(clk)                                        \
    AA_EN | Z_CMP | IM_RD | CVG_DST_WRAP | ALPHA_CVG_SEL | ZMODE_DEC | \
        GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_A_MEM)

#define RM_AA_ZB_XLU_DECAL(clk)                                                \
    AA_EN | Z_CMP | IM_RD | CVG_DST_WRAP | CLR_ON_CVG | FORCE_BL | ZMODE_DEC | \
        GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA)

#define G_RM_AA_ZB_OPA_SURF RM_AA_ZB_OPA_SURF(1)
#define G_RM_AA_ZB_OPA_SURF2 RM_AA_ZB_OPA_SURF(2)
#define G_RM_AA_ZB_TEX_EDGE RM_AA_ZB_TEX_EDGE(1)
#define G_RM_AA_ZB_TEX_EDGE2 RM_AA_ZB_TEX_EDGE(2)
#define G_RM_AA_ZB_XLU_SURF RM_AA_ZB_XLU_SURF(1)
#define G_RM_AA_ZB_XLU_SURF2 RM_AA_ZB_XLU_SURF(2)
#define G_RM_AA_ZB_OPA_DECAL RM_AA_ZB_OPA_DECAL(1)
#define G_RM_AA_ZB_OPA_DECAL2 RM_AA_ZB_OPA_DECAL(2)
#define G_RM_AA_ZB_XLU_DECAL RM_AA_ZB_XLU_DECAL(1)
#define G_RM_AA_ZB_XLU_DECAL2 RM_AA_ZB_XLU_DECAL(2)

#define gsSPGeometryMode(c, s)                                                       \
    {                                                                                \
        { (_SHIFTL(G_GEOMETRYMODE, 24, 8) | _SHIFTL(~(u32) (c), 0, 24)), (u32) (s) } \
    }

#define G_TEXTURE_ENABLE 0x00000002 /* Microcode use only */
#define G_SHADING_SMOOTH 0x00000200 /* flat or smooth shaded */
#define G_CULL_FRONT 0x00001000
#define G_CULL_BACK 0x00002000
#define G_CULL_BOTH 0x00003000 /* To make code cleaner */
#define gsSPSetGeometryMode(word)                                    \
    {                                                                \
        { _SHIFTL(G_SETGEOMETRYMODE, 24, 8), (unsigned int) (word) } \
    }

#define gsSPClearGeometryMode(word)                                    \
    {                                                                  \
        { _SHIFTL(G_CLEARGEOMETRYMODE, 24, 8), (unsigned int) (word) } \
    }
#define gsSPCullDisplayList(vstart, vend)                                                       \
    {                                                                                           \
        { _SHIFTL(G_CULLDL, 24, 8) | _SHIFTL((vstart) * 2, 0, 16), _SHIFTL((vend) * 2, 0, 16) } \
    }
#define G_ON (1)
#define G_OFF (0)

#define NUML(n) (((n) + 1) * 32 + 0x80000000)
#define NUMLIGHTS_0 1
#define NUMLIGHTS_1 1
#define NUMLIGHTS_2 2
#define NUMLIGHTS_3 3
#define NUMLIGHTS_4 4
#define NUMLIGHTS_5 5
#define NUMLIGHTS_6 6
#define NUMLIGHTS_7 7

#define G_MW_NUMLIGHT 0x02

#define G_MWO_NUMLIGHT 0x00

#define gsImmp21(c, p0, p1, dat) \
    { _SHIFTL((c), 24, 8) | _SHIFTL((p0), 8, 16) | _SHIFTL((p1), 0, 8), (uintptr_t)(dat) }
#define gsMoveWd(index, offset, data) gsImmp21(G_MOVEWORD, offset, index, data)
#define gsSPNumLights(n) gsMoveWd(G_MW_NUMLIGHT, G_MWO_NUMLIGHT, NUML(n))

#define gsDPSetTile(fmt, siz, line, tmem, tile, palette, cmt, maskt, shiftt, cms, masks, shifts)            \
    {                                                                                                       \
        (_SHIFTL(G_SETTILE, 24, 8) | _SHIFTL(fmt, 21, 3) | _SHIFTL(siz, 19, 2) | _SHIFTL(line, 9, 9) |      \
         _SHIFTL(tmem, 0, 9)),                                                                              \
            (_SHIFTL(tile, 24, 3) | _SHIFTL(palette, 20, 4) | _SHIFTL(cmt, 18, 2) | _SHIFTL(maskt, 14, 4) | \
             _SHIFTL(shiftt, 10, 4) | _SHIFTL(cms, 8, 2) | _SHIFTL(masks, 4, 4) | _SHIFTL(shifts, 0, 4))    \
    }

#define G_TX_RENDERTILE 0

#define gsDPLoadTileGeneric(c, tile, uls, ult, lrs, lrt)                      \
    {                                                                         \
        _SHIFTL(c, 24, 8) | _SHIFTL(uls, 12, 12) | _SHIFTL(ult, 0, 12),       \
            _SHIFTL(tile, 24, 3) | _SHIFTL(lrs, 12, 12) | _SHIFTL(lrt, 0, 12) \
    }

#define gsDPSetTileSize(t, uls, ult, lrs, lrt) gsDPLoadTileGeneric(G_SETTILESIZE, t, uls, ult, lrs, lrt)

#define gsDma1p(c, s, l, p) \
    { (_SHIFTL((c), 24, 8) | _SHIFTL((p), 16, 8) | _SHIFTL((l), 0, 16)), (uintptr_t)(s) }
#define gsSPVertex(v, n, v0) gsDma1p(G_VTX, (v), ((n) << 10) | (0x10 * (n)-1), (v0)*2)

#define __gsSP1Triangle_w1(v0, v1, v2) (_SHIFTL((v0)*2, 16, 8) | _SHIFTL((v1)*2, 8, 8) | _SHIFTL((v2)*2, 0, 8))
#define __gsSP1Triangle_w1f(v0, v1, v2, flag)         \
    (((flag) == 0)   ? __gsSP1Triangle_w1(v0, v1, v2) \
     : ((flag) == 1) ? __gsSP1Triangle_w1(v1, v2, v0) \
                     : __gsSP1Triangle_w1(v2, v0, v1))
#define gsSP1Triangle(v0, v1, v2, flag) \
    { _SHIFTL(G_TRI1, 24, 8), __gsSP1Triangle_w1f(v0, v1, v2, flag) }

#define gsSP2Triangles(v00, v01, v02, flag0, v10, v11, v12, flag1) \
    { (_SHIFTL(G_TRI2, 24, 8) | __gsSP1Triangle_w1f(v00, v01, v02, flag0)), __gsSP1Triangle_w1f(v10, v11, v12, flag1) }

#define G_DL_PUSH 0x00
#define gsSPDisplayList(dl) gsDma1p(G_DL, dl, 0, G_DL_PUSH)

#define gsSetImage(cmd, fmt, siz, width, i) \
    { _SHIFTL(cmd, 24, 8) | _SHIFTL(fmt, 21, 3) | _SHIFTL(siz, 19, 2) | _SHIFTL((width)-1, 0, 12), (uintptr_t)(i) }

#define gsDPSetTextureImage(f, s, w, i) gsSetImage(G_SETTIMG, f, s, w, i)

#define G_TX_LDBLK_MAX_TXL 4095

#define gsDPLoadBlock(tile, uls, ult, lrs, dxt)                                                            \
    {                                                                                                      \
        (_SHIFTL(G_LOADBLOCK, 24, 8) | _SHIFTL(uls, 12, 12) | _SHIFTL(ult, 0, 12)),                        \
            (_SHIFTL(tile, 24, 3) | _SHIFTL((MIN(lrs, G_TX_LDBLK_MAX_TXL)), 12, 12) | _SHIFTL(dxt, 0, 12)) \
    }

#define G_TX_NOMIRROR 0
#define G_TX_WRAP 0

#define G_TX_NOMASK 0
#define G_TX_NOLOD 0

// Packed opcodes (alignés avec src/racing/memory.c)
enum PackedOp : uint8_t {
    PG_LIGHTS_0                  = 0x00, // 0..0x14 mappés sur unpack_lights côté runtime
    PG_SETCOMBINE_CC_MODULATERGBA      = 0x15,
    PG_SETCOMBINE_CC_MODULATERGBDECALA = 0x16,
    PG_SETCOMBINE_CC_SHADE             = 0x17,
    PG_RMODE_OPA              = 0x18,
    PG_RMODE_TEXEDGE          = 0x19,
    PG_TILECFG_A              = 0x1A,
    PG_TILECFG_B              = 0x1B,
    PG_TILECFG_C              = 0x1C,
    PG_TILECFG_D              = 0x1D,
    PG_TILECFG_E              = 0x1E,
    PG_TILECFG_F              = 0x1F,
    PG_TIMG_LOADBLOCK_0       = 0x20,
    PG_TIMG_LOADBLOCK_1       = 0x21,
    PG_TIMG_LOADBLOCK_2       = 0x22,
    PG_TIMG_LOADBLOCK_3       = 0x23,
    PG_TIMG_LOADBLOCK_4       = 0x24,
    PG_TIMG_LOADBLOCK_5       = 0x25,
    PG_TEXTURE_ON             = 0x26,
    PG_TEXTURE_OFF            = 0x27,
    PG_VTX1                   = 0x28,
    PG_TRI1                   = 0x29,
    PG_ENDDL                  = 0x2A,
    PG_DL                     = 0x2B,
    PG_TILECFG_G              = 0x2C,
    PG_CULLDL                 = 0x2D,
    PG_SETCOMBINE_ALT         = 0x2E,
    PG_RMODE_XLU              = 0x2F,
    PG_SPLINE3D               = 0x30,
    PG_VTX_BASE               = 0x32, // 0x33..0x52 VTX2 variants
    PG_SETCOMBINE_CC_DECALRGBA= 0x53,
    PG_RMODE_OPA_DECAL        = 0x54,
    PG_RMODE_XLU_DECAL        = 0x55,
    PG_SETGEOMETRYMODE        = 0x56,
    PG_CLEARGEOMETRYMODE      = 0x57,
    PG_TRI2                   = 0x58,
    PG_EOF                    = 0xFF,
};

std::string opcode_to_string(uint8_t op) {
    if (PG_LIGHTS_0 <= op && op <= PG_LIGHTS_0 + 0x20) {
        return "PG_LIGHTS_" + std::to_string(op - PG_LIGHTS_0);
    }
    if (PG_VTX_BASE <= op && op <= PG_VTX_BASE + 0x20) {
        return "PG_VTX_" + std::to_string(op - PG_VTX_BASE);
    }
    switch (op) {
        case PG_SETCOMBINE_CC_MODULATERGBA: return "PG_SETCOMBINE_CC_MODULATERGBA";
        case PG_SETCOMBINE_CC_MODULATERGBDECALA: return "PG_SETCOMBINE_CC_MODULATERGBDECALA";
        case PG_SETCOMBINE_CC_SHADE: return "PG_SETCOMBINE_CC_SHADE";
        case PG_RMODE_OPA: return "PG_RMODE_OPA";
        case PG_RMODE_TEXEDGE: return "PG_RMODE_TEXEDGE";
        case PG_TILECFG_A: return "PG_TILECFG_A";
        case PG_TILECFG_B: return "PG_TILECFG_B";
        case PG_TILECFG_C: return "PG_TILECFG_C";
        case PG_TILECFG_D: return "PG_TILECFG_D";
        case PG_TILECFG_E: return "PG_TILECFG_E";
        case PG_TILECFG_F: return "PG_TILECFG_F";
        case PG_TIMG_LOADBLOCK_0: return "PG_TIMG_LOADBLOCK_0";
        case PG_TIMG_LOADBLOCK_1: return "PG_TIMG_LOADBLOCK_1";
        case PG_TIMG_LOADBLOCK_2: return "PG_TIMG_LOADBLOCK_2";
        case PG_TIMG_LOADBLOCK_3: return "PG_TIMG_LOADBLOCK_3";
        case PG_TIMG_LOADBLOCK_4: return "PG_TIMG_LOADBLOCK_4";
        case PG_TIMG_LOADBLOCK_5: return "PG_TIMG_LOADBLOCK_5";
        case PG_TEXTURE_ON: return "PG_TEXTURE_ON";
        case PG_TEXTURE_OFF: return "PG_TEXTURE_OFF";
        case PG_VTX1: return "PG_VTX1";
        case PG_TRI1: return "PG_TRI1";
        case PG_ENDDL: return "PG_ENDDL";
        case PG_DL: return "PG_DL";
        case PG_TILECFG_G: return "PG_TILECFG_G";
        case PG_CULLDL: return "PG_CULLDL";
        case PG_SETCOMBINE_ALT: return "PG_SETCOMBINE_ALT";
        case PG_RMODE_XLU: return "PG_RMODE_XLU";
        case PG_SPLINE3D: return "PG_SPLINE3D";
        case PG_SETCOMBINE_CC_DECALRGBA: return "PG_SETCOMBINE_CC_DECALRGBA";
        case PG_RMODE_OPA_DECAL: return "PG_RMODE_OPA_DECAL";
        case PG_RMODE_XLU_DECAL: return "PG_RMODE_XLU_DECAL";
        case PG_SETGEOMETRYMODE: return "PG_SETGEOMETRYMODE";
        case PG_CLEARGEOMETRYMODE: return "PG_CLEARGEOMETRYMODE";
        case PG_TRI2: return "PG_TRI2";
        case PG_EOF: return "PG_EOF";
        default: return "PG_UNKNOWN_" + std::to_string(op);
    }
}

static inline uint32_t ImmediateSize(uint8_t op) {
    switch (op) {
        case PG_TRI1: return 2;              // packed 2 bytes
        case PG_TRI2: return 4;              // packed 4 bytes (2 tris)
        case PG_DL: return 2;                // index
        case PG_VTX1: return 4;              // vtx1: 4 bytes
        case PG_SPLINE3D: return 3;          // spline3d: 3 bytes
        case PG_SETCOMBINE_CC_MODULATERGBA:
        case PG_SETCOMBINE_CC_MODULATERGBDECALA:
        case PG_SETCOMBINE_CC_SHADE:
        case PG_RMODE_OPA:
        case PG_RMODE_TEXEDGE:
        case PG_SETCOMBINE_ALT:
        case PG_RMODE_XLU:
        case PG_SETCOMBINE_CC_DECALRGBA:
        case PG_RMODE_OPA_DECAL:
        case PG_RMODE_XLU_DECAL:
        case PG_SETGEOMETRYMODE:
        case PG_CLEARGEOMETRYMODE:
            return 0;                        // pas d’immediate dans notre format packé
        case PG_TIMG_LOADBLOCK_0:
        case PG_TIMG_LOADBLOCK_1:
        case PG_TIMG_LOADBLOCK_2:
        case PG_TIMG_LOADBLOCK_3:
        case PG_TIMG_LOADBLOCK_4:
        case PG_TIMG_LOADBLOCK_5:
            return 3;                        // fmt-dep tex, 3 bytes
        case PG_TILECFG_A:
        case PG_TILECFG_B:
        case PG_TILECFG_C:
        case PG_TILECFG_D:
        case PG_TILECFG_E:
        case PG_TILECFG_F:
        case PG_TILECFG_G:
            return 2;                        // 2 bytes
        default:
            // VTX2 banked range: 0x33..0x52 → 2 bytes
            if (op >= (uint8_t)(PG_VTX_BASE) && op <= (uint8_t)(PG_VTX_BASE + 0x20)) {
                // base (0x32) unused, but if encountered, treat as 0
                return (op == PG_VTX_BASE) ? 0 : 2;
            }
            return 0;
    }
}

static inline uint16_t RD16(const std::vector<uint8_t>& b, size_t i) {
    return (uint16_t)((b[i + 1] << 8) | b[i]);
}

std::optional<std::shared_ptr<IParsedData>> PackedDListFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    auto [_, segment] = Decompressor::AutoDecode(data, buffer);
    std::vector<uint8_t> decoded(segment.data, segment.data + segment.size);

    const auto start = 0;
    size_t i = start;
    std::vector<uint32_t> gfx;

    auto emit = [&](uint32_t w0, uint32_t w1) {
        gfx.push_back(w0);
        gfx.push_back(w1);
    };

    // Minimal expansion for export purposes
    std::vector<uint8_t>& raw = buffer; // pour compatibilité éventuelle avec le reste du code
    YAML::Node& node = data;

    while (i < decoded.size()) {
        uint8_t op = decoded[i++];
        SPDLOG_INFO("PackedDListFactory: opcode 0x{:02X} ({})", op, opcode_to_string(op));
        if (op == PG_EOF) { break; }

        // LIGHTS: 0x00..0x14
        if (op >= PG_LIGHTS_0 && op <= PG_LIGHTS_0 + 0x14) {
            int a = (op * 0x18) + 0x9000008;
            int b = (op * 0x18) + 0x9000000;
            N64Gfx macro = gsSPNumLights(NUMLIGHTS_1);
            emit(macro.words.w0, macro.words.w1);
            emit(0x3860010, a);
            emit(0x3880010, b);
            continue;
        }

        if (op >= (uint8_t)(PG_VTX_BASE + 0x01) && op <= (uint8_t)(PG_VTX_BASE + 0x20)) {
            // Mimic unpack_vtx2: banked variant encodes count in opcode
            if (i + 2 > decoded.size()) { goto done; }
            uintptr_t vtxOff = RD16(decoded, i);
            i += 2;
            vtxOff *= 0x10; // bytes

            uint8_t n = op - PG_VTX_BASE; // map 0x33..→1.. etc.
            // In runtime, w0 uses (n<<10)+((n*0x10)-1); start index implied 0
            // uint32_t w0 = (_SHIFTL(G_VTX, 24, 8)
            //               | ((n << 10) + ((n * 0x10) - 1)));
            // uint32_t w1 = vtxOff;
            N64Gfx macro = gsSPVertex(vtxOff, n, 0);
            emit(macro.words.w0, macro.words.w1);
            continue;
        }

        switch (op) {
            case PG_VTX1: {
                // Mimic unpack_vtx1
                if (i + 4 > decoded.size()) { goto done; }
                uint8_t t0 = decoded[i++];
                uint16_t vtxOff = ((uint16_t)decoded[i++] << 8) | t0;
                vtxOff *= 0x10; // bytes

                uint8_t b0 = decoded[i++];
                uint8_t start = (uint8_t)(b0 & 0x3F);
                
                uint8_t b1 = decoded[i++];
                uint8_t n = (uint8_t)(b1 & 0x3F);

                // G_VTX encoding matches DisplayListFactory exporter expectations
                uint32_t w0 = (_SHIFTL(G_VTX, 24, 8)
                            | _SHIFTL((n * 2), 16, 8)
                            | ((start << 10) + ((0x10 * start) - 1)));
                // Use segmented address style: segment 0x04 is vertex pool in runtime; here we keep raw offset so exporter can attempt resolution
                uint32_t w1 = vtxOff; // exporter will patch/resolve if possible
                
                emit(w0, w1);
            } break;
            case PG_TILECFG_A:
            case PG_TILECFG_B:
            case PG_TILECFG_C:
            case PG_TILECFG_D:
            case PG_TILECFG_E:
            case PG_TILECFG_F:
            case PG_TILECFG_G: {
                // Mirror de unpack_tile_sync
                if (i + 2 > decoded.size()) { goto done; }
                int width = 32, height = 32, fmt = 0, tmem = 0;
                switch (op) {
                    case PG_TILECFG_A: width = 32; height = 32; fmt = 0; tmem = 0; break;
                    case PG_TILECFG_B: width = 64; height = 32; fmt = 0; tmem = 0; break;
                    case PG_TILECFG_C: width = 32; height = 64; fmt = 0; tmem = 0; break;
                    case PG_TILECFG_D: width = 32; height = 32; fmt = 3; tmem = 0; break;
                    case PG_TILECFG_E: width = 64; height = 32; fmt = 3; tmem = 0; break;
                    case PG_TILECFG_F: width = 32; height = 64; fmt = 3; tmem = 0; break;
                    case PG_TILECFG_G: width = 32; height = 32; fmt = 0; tmem = 256; break;
                }
                const int sizBytes = G_IM_SIZ_16b_BYTES; // 2
                const int line = (((width * 2) + 7) >> 3);

                uint8_t b0 = decoded[i++];
                uint8_t b1 = decoded[i++];
                const int cms = b0 & 0xF;
                const int masks = (b0 >> 4) & 0xF;
                const int cmt = b1 & 0xF;
                const int maskt = (b1 >> 4) & 0xF;

                // gsDPTileSync
                {
                    N64Gfx macro = gsDPTileSync();
                    emit(macro.words.w0, macro.words.w1);
                }

                // G_SETTILE
                // uint32_t w0 = (_SHIFTL(G_SETTILE, 24, 8) | _SHIFTL(fmt, 21, 3) | _SHIFTL(G_IM_SIZ_16b_BYTES, 19, 2) | _SHIFTL(line, 9, 9) | _SHIFTL(tmem, 0, 9));
                // uint32_t w1 = (_SHIFTL(cmt, 18, 3) | _SHIFTL(maskt, 14, 4) | _SHIFTL(cms, 8, 3) | _SHIFTL(masks, 4, 4));
                N64Gfx macro = gsDPSetTile(fmt, G_IM_SIZ_16b, line, tmem, G_TX_RENDERTILE, 0, cmt, maskt, 0, cms, masks, 0);
                emit(macro.words.w0, macro.words.w1);

                // G_SETTILESIZE
                // const int lrs = (width - 1) << 2;
                // const int lrt = (height - 1) << 2;
                // emit(_SHIFTL(G_SETTILESIZE, 24, 8), _SHIFTL(lrs, 12, 12) | _SHIFTL(lrt, 0, 12));
                macro = gsDPSetTileSize(G_TX_RENDERTILE, 0, 0, (width - 1) << 2, (height - 1) << 2);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_TIMG_LOADBLOCK_0:
            case PG_TIMG_LOADBLOCK_1:
            case PG_TIMG_LOADBLOCK_2:
            case PG_TIMG_LOADBLOCK_3:
            case PG_TIMG_LOADBLOCK_4:
            case PG_TIMG_LOADBLOCK_5: {
                // Mirror de unpack_tile_load_sync
                if (i + 3 > decoded.size()) { goto done; }
                uint32_t width = 32, height = 32, fmt = 0;
                switch (op) {
                    case PG_TIMG_LOADBLOCK_0: width = 32; height = 32; fmt = 0; break;
                    case PG_TIMG_LOADBLOCK_1: width = 64; height = 32; fmt = 0; break;
                    case PG_TIMG_LOADBLOCK_2: width = 32; height = 64; fmt = 0; break;
                    case PG_TIMG_LOADBLOCK_3: width = 32; height = 32; fmt = 3; break;
                    case PG_TIMG_LOADBLOCK_4: width = 64; height = 32; fmt = 3; break;
                    case PG_TIMG_LOADBLOCK_5: width = 32; height = 64; fmt = 3; break;
                }
                uint32_t offset = ((uint32_t)decoded[i++]) << 11; // index << 11

                i++;
                uint8_t arg = decoded[i++];
                const uint32_t siz = G_IM_SIZ_16b;
                const uint32_t tmem = (arg & 0x0F);
                const uint32_t tile = ((arg >> 4) & 0x0F);

                // G_SETTIMG (on garde un pointeur brut = offset; l'exporteur patchera en OTR)
                // Utiliser width=1 par défaut pour le champ "width-1" du w0
                // uint32_t w0 = (_SHIFTL(G_SETTIMG, 24, 8) | _SHIFTL(fmt, 21, 3) | _SHIFTL(siz, 19, 2));
                N64Gfx macro = gsDPSetTextureImage(fmt, siz, 1, offset);
                
                emit(macro.words.w0, macro.words.w1);

                // gsDPTileSync
                {
                    N64Gfx macro = gsDPTileSync();
                    emit(macro.words.w0, macro.words.w1);
                }

                // G_SETTILE (fmt/siz/tmem) + tile en w1
                // w0 = (_SHIFTL(G_SETTILE, 24, 8) | _SHIFTL(fmt, 21, 3) | _SHIFTL(siz, 19, 2) | _SHIFTL(tmem, 0, 9));
                // uint32_t w1 = _SHIFTL(tile, 24, 3);
                macro = gsDPSetTile(fmt, siz, 0, tmem, tile, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
                emit(macro.words.w0, macro.words.w1);

                // gsDPLoadSync
                {
                    N64Gfx macro = gsDPLoadSync();
                    emit(macro.words.w0, macro.words.w1);
                }

                // G_LOADBLOCK
                // const uint32_t count = std::min((width * height) - 1, 0x7FFu);
                const uint32_t dxt = CALC_DXT(width, G_IM_SIZ_16b_BYTES);
                // w0 = _SHIFTL(G_LOADBLOCK, 24, 8);
                // w1 = (_SHIFTL(tile, 24, 3) | _SHIFTL(count, 12, 12) | _SHIFTL(dxt, 0, 12));
                macro = gsDPLoadBlock(tile, 0, 0, (width * height) - 1, dxt);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_TEXTURE_ON: {
                N64Gfx macro = gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_TEXTURE_OFF: {
                // gsSPTexture(0x1, 0x1, 0, G_TX_RENDERTILE, G_OFF)
                N64Gfx macro = gsSPTexture(0x1, 0x1, 0, G_TX_RENDERTILE, G_OFF);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_SETCOMBINE_CC_MODULATERGBA: {
                // gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_MODULATERGBA)
                N64Gfx macro = gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_MODULATERGBA);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_SETCOMBINE_CC_MODULATERGBDECALA: {
                N64Gfx macro = gsDPSetCombineMode(G_CC_MODULATERGBDECALA, G_CC_MODULATERGBDECALA);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_SETCOMBINE_CC_SHADE: {
                N64Gfx macro = gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_SETCOMBINE_ALT: {
                // Alias historique → même que MODULATERGBDECALA côté runtime
                N64Gfx macro = gsDPSetCombineMode(G_CC_MODULATERGBDECALA, G_CC_MODULATERGBDECALA);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_SETCOMBINE_CC_DECALRGBA: {
                N64Gfx macro = gsDPSetCombineMode(G_CC_DECALRGBA, G_CC_DECALRGBA);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_RMODE_OPA: {
                N64Gfx macro = gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_RMODE_TEXEDGE: {
                N64Gfx macro = gsDPSetRenderMode(G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_RMODE_XLU: {
                N64Gfx macro = gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_RMODE_OPA_DECAL: {
                N64Gfx macro = gsDPSetRenderMode(G_RM_AA_ZB_OPA_DECAL, G_RM_AA_ZB_OPA_DECAL);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_RMODE_XLU_DECAL: {
                N64Gfx macro = gsDPSetRenderMode(G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_SETGEOMETRYMODE: {
                N64Gfx macro = gsSPSetGeometryMode(G_CULL_BACK);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_CLEARGEOMETRYMODE: {
                N64Gfx macro = gsSPClearGeometryMode(G_CULL_BACK);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_CULLDL: {
                N64Gfx macro = gsSPCullDisplayList(0, 7);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_TRI1: {
                if (i + 2 > decoded.size()) { goto done; }
                uint16_t c = RD16(decoded, i); i += 2;
                uint8_t a = (c & 0x1F);
                uint8_t b = ((c >> 5) & 0x1F);
                uint8_t d = ((c >> 10) & 0x1F);
                N64Gfx macro = gsSP1Triangle(a, b, d, 0);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_TRI2: {
                if (i + 4 > decoded.size()) { goto done; }
                uint16_t c0 = RD16(decoded, i); i += 2;
                uint16_t c1 = RD16(decoded, i); i += 2;
                uint8_t a0 = (c0 & 0x1F);
                uint8_t b0 = ((c0 >> 5) & 0x1F);
                uint8_t d0 = ((c0 >> 10) & 0x1F);
                uint8_t a1 = (c1 & 0x1F);
                uint8_t b1 = ((c1 >> 5) & 0x1F);
                uint8_t d1 = ((c1 >> 10) & 0x1F);
                N64Gfx macro = gsSP2Triangles(a0, b0, d0, 0, a1, b1, d1, 0);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_SPLINE3D: {
                // Mimic unpack_spline_3D: map packed 3 bytes into a QUAD macro layout
                if (i + 3 > decoded.size()) { goto done; }
                uint8_t b0 = decoded[i++];
                uint8_t b1 = decoded[i++];
                uint8_t b2 = decoded[i++];

                // Follow non-mirror path from memory.c
                uint8_t t0 = (uint8_t)(b0 & 0x1F);
                uint8_t a3 = (uint8_t)((b0 >> 5) & 0x7);
                a3 |= (uint8_t)((b1 & 0x3) * 8);
                uint8_t a2 = (uint8_t)((b1 >> 2) & 0x1F);
                uint8_t a0 = (uint8_t)(((b1 >> 7) & 0x1) | ((b2 & 0xF) * 2));

                uint32_t w0 = _SHIFTL(G_QUAD, 24, 8);
                uint32_t w1 = (_SHIFTL((uint8_t)(a0 * 2), 24, 8)
                              | _SHIFTL((uint8_t)(t0 * 2), 16, 8)
                              | _SHIFTL((uint8_t)(a3 * 2), 8, 8)
                              | _SHIFTL((uint8_t)(a2 * 2), 0, 8));
                emit(w0, w1);
            } break;
            case PG_DL: {
                if (i + 2 > decoded.size()) { goto done; }
                uint16_t idx = RD16(decoded, i); i += 2;
                // uint32_t w0 = (_SHIFTL(G_DL, 24, 8)); // push
                // // Encode as segmented address into segment 0x07 so exporter treats it as an index into packed DL buffer
                // uint32_t w1 = 0x07000000u | (idx * 8);
                N64Gfx macro = gsSPDisplayList(idx);
                emit(macro.words.w0, macro.words.w1);
            } break;
            case PG_ENDDL:
                emit(_SHIFTL(G_ENDDL, 24, 8), 0);
                SPDLOG_INFO("Size of packed DL: 0x{:X}", i - start);
                goto done;
        default: {
                // Skip immediates for any known opcode (tilecfg, timg, spline, etc.)
                uint32_t imm = ImmediateSize(op);
                if (imm) {
            if (i + imm > decoded.size()) { goto done; }
                    i += imm;
                }
                // Other 1-byte ops (combine, render, texture on/off, geom modes) produce no gfx here
            } break;
        }
    }

done:
    if (gfx.empty()) {
        return std::nullopt;
    }

    // Print gfx command output for debugging/export visualization
    for (size_t j = 0; j + 1 < gfx.size(); j += 2) {
        SPDLOG_INFO("PackedDL gfx[{}]: W0=0x{:08X} W1=0x{:08X}", j / 2, gfx[j], gfx[j + 1]);
    }

#ifdef STANDALONE
    // Pretty-print the generated commands using gfxd (same tooling as DisplayListFactory)
    gfxd_input_buffer(gfx.data(), sizeof(uint32_t) * gfx.size());
    gfxd_output_fd(fileno(stdout));
    gfxd_endian(gfxd_endian_host, sizeof(uint32_t));
    gfxd_macro_fn([] {
        gfxd_puts("> ");
        gfxd_macro_dflt();
        gfxd_puts("\n");
        return 0;
    });
    // No overrides needed here; just default decode
    // Version selection mirrors DisplayListFactory
    gfxd_target(gfxd_f3dex);
    gfxd_execute();
#endif
    return std::make_shared<DListData>(gfx);
}
