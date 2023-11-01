#include "DisplayListFactory.h"

#include <iostream>

#include "n64/gbi.h"
#include "utils/MIODecoder.h"
#include "n64/CommandMacros.h"
#include "Companion.h"
#include "binarytools/BinaryReader.h"
#include "spdlog/spdlog.h"

#define GFX_SIZE 8

#define gsDPSetCombineLERP_NoMacros(a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0,      \
        a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1)         \
{                                   \
    _SHIFTL(G_SETCOMBINE, 24, 8) |                  \
    _SHIFTL(GCCc0w0(a0, c0,         \
               Aa0, Ac0) |          \
           GCCc1w0(a1, c1), 0, 24),     \
    (unsigned int)(GCCc0w1(b0, d0,      \
                   Ab0, Ad0) |      \
               GCCc1w1(b1, Aa1,     \
                   Ac1, d1,     \
                   Ab1, Ad1))       \
}

#define gsSPBranchLessZraw2(dl, vtx, zval)               \
{   _SHIFTL(G_BRANCH_Z,24,8)|_SHIFTL((vtx)*5,12,12)|_SHIFTL((vtx)*2,0,12),\
    (unsigned int)(zval),                       }

#define gsSPBranchLessZraw3(dl)               \
{   _SHIFTL(G_RDPHALF_1,24,8),                        \
    (unsigned int)(dl),                     }

#define gsDPWordLo(wordlo)            \
    gsImmp1(G_RDPHALF_2, (unsigned int)(wordlo))

#define gsSPTextureRectangle2(xl, yl, xh, yh, tile)    \
{    (_SHIFTL(G_TEXRECT, 24, 8) | _SHIFTL(xh, 12, 12) | _SHIFTL(yh, 0, 12)),\
    (_SHIFTL(tile, 24, 3) | _SHIFTL(xl, 12, 12) | _SHIFTL(yl, 0, 12)) }

typedef struct {
    uint32_t w0;
    uint32_t w1;
} __attribute__((packed, aligned(1))) BGfx;

uint64_t ReadU64BE(uint8_t* data, uint32_t offset){
    uint64_t value;
    memcpy(&value, data + offset, 8);
    return BSWAP64(value);
}

bool DisplayListFactory::process(LUS::BinaryWriter* writer, YAML::Node& node, std::vector<uint8_t>& buffer) {

    WriteHeader(writer, LUS::ResourceType::DisplayList, 0);

    while (writer->GetBaseAddress() % 8 != 0)
        writer->Write((uint8_t)0xFF);

    auto offset = node["offset"].as<size_t>();
    uint8_t* list;

    if(node["mio0"]){
        auto mio0 = node["mio0"].as<size_t>();
        auto decoded = MIO0Decoder::Decode(buffer, mio0);
        list = (uint8_t*) (decoded.data() + offset);
    } else {
        list = (uint8_t*) buffer.data() + offset;
    }

    uint32_t cursor = 0;
    bool processing = true;

    while(processing){
        uint64_t data = ReadU64BE(list, cursor);
        uint32_t w0 = 0;
        uint32_t w1 = data & 0xFFFFFFFF;

        auto _unused = (data >> 56);
        uint8_t opcode = (uint8_t)(data >> 56);
        uint32_t replacement = opcode;

        if ((int) opcode == G_DL) {
            replacement = (uint8_t) G_DL_OTR_HASH;
        }

        if ((int) opcode == G_MTX) {
            replacement = (uint8_t) G_MTX_OTR;
        }

        if ((int) opcode == G_VTX) {
            replacement = (uint8_t) G_VTX_OTR_HASH;
        }

        if ((int) opcode == G_SETTIMG) {
            replacement = (uint8_t) G_SETTIMG_OTR_HASH;
        }

        w0 += (replacement << 24);

#define case case (uint8_t)

        switch (opcode) {
            case G_ENDDL: {
                processing = false;
                break;
            }
            case G_SETTIMG: {
                uint32_t ptr = SEGMENT_OFFSET(w1);
                auto decl = Companion::Instance->GetNodeByAddr(ptr);

                int32_t __ = (data & 0x00FF000000000000) >> 48;
                int32_t www = (data & 0x00000FFF00000000) >> 32;

                uint32_t fmt = (__ & 0xE0) >> 5;
                uint32_t siz = (__ & 0x18) >> 3;

                Gfx value = {gsDPSetTextureImage(fmt, siz, www + 1, __)};
                w0 = value.words.w0 & 0x00FFFFFF;
                w0 += (G_SETTIMG_OTR_HASH << 24);
                w1 = 0;

                writer->Write(w0);
                writer->Write(w1);

                if(decl.has_value()){
                    uint64_t hash = CRC64(std::get<0>(decl.value()).c_str());
                    SPDLOG_INFO("Found texture: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(decl.value()));
                    w0 = hash >> 32;
					w1 = hash & 0xFFFFFFFF;
                } else {
                    SPDLOG_INFO("Warning: Could not find texture at 0x{:X}", ptr);
                    w0 = 0;
                    w1 = 0;
                }
                break;
            }
            default: {
                SPDLOG_INFO("Found Opcode {:X}", opcode);
                w0 = (uint32_t) (data >> 32);
                w1 = (uint32_t) (data & 0xFFFFFFFF);
            }
        }

        writer->Write(w0);
        writer->Write(w1);
        cursor += 8;

//        if(opcode == G_DL) {
//            opcode = G_DL_OTR_HASH;
//        }
//
//        if(opcode == G_MTX) {
//            opcode = G_MTX_OTR;
//        }
//
//        if(opcode == G_BRANCH_Z) {
//            opcode = G_BRANCH_Z_OTR;
//        }
//
//        if(opcode == G_VTX) {
//            opcode = G_VTX_OTR_HASH;
//        }
//
//        if(opcode == G_SETTIMG) {
//            opcode = G_SETTIMG_OTR_HASH;
//        }

//        switch (opcode) {
//            case G_NOOP: {
//                Gfx value = {gsDPNoOp()};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//		    case G_MODIFYVTX: {
//                int32_t ww = (data & 0x00FF000000000000ULL) >> 48;
//                int32_t nnnn = (data & 0x0000FFFF00000000ULL) >> 32;
//                int32_t vvvvvvvv = (data & 0x00000000FFFFFFFFULL);
//
//                Gfx value = {gsSPModifyVertex(nnnn / 2, ww, vvvvvvvv)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_GEOMETRYMODE: {
//                int32_t cccccc = (data & 0x00FFFFFF00000000) >> 32;
//                int32_t ssssssss = (data & 0xFFFFFFFF);
//
//                Gfx value = {gsSPGeometryMode(~cccccc, ssssssss)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_RDPLOADSYNC: {
//                Gfx value = {gsDPPipeSync()};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_RDPSETOTHERMODE: {
//                int32_t hhhhhh = (data & 0x00FFFFFF00000000) >> 32;
//                int32_t llllllll = (data & 0x00000000FFFFFFFF);
//
//                Gfx value = {gsDPSetOtherMode(hhhhhh, llllllll)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_POPMTX: {
//                Gfx value = {gsSPPopMatrix(data)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_SETENVCOLOR: {
//                uint8_t r = (uint8_t)((data & 0xFF000000) >> 24);
//                uint8_t g = (uint8_t)((data & 0x00FF0000) >> 16);
//                uint8_t b = (uint8_t)((data & 0xFF00FF00) >> 8);
//                uint8_t a = (uint8_t)((data & 0x000000FF) >> 0);
//
//                Gfx value = {gsDPSetEnvColor(r, g, b, a)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_MTX: {
//                uint32_t pp = (data & 0x000000FF00000000) >> 32;
//                uint32_t mm = (data & 0x00000000FFFFFFFF);
//                pp ^= G_MTX_PUSH;
//
//                Gfx value = {gsSPMatrix(mm, pp)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//
//                w0 = (w0 & 0x00FFFFFF) + (G_MTX_OTR << 24);
//
//                auto mtxDecl = Companion::Instance->GetNodeByAddr(SEGMENT_OFFSET(mm));
//
//                writer->Write(w0);
//                writer->Write(w1);
//
//                if (mtxDecl.has_value()) {
//                    uint64_t hash = CRC64(std::get<0>(mtxDecl.value()).c_str());
//                    w0 = hash >> 32;
//                    w1 = hash & 0xFFFFFFFF;
//                } else {
//                    w0 = 0;
//                    w1 = 0;
//                    SPDLOG_INFO("Warning: Could not find matrix at 0x{:X}", mm);
//                }
//                break;
//            }
//            case G_LOADBLOCK: {
//                int32_t sss = (data & 0x00FFF00000000000) >> 48;
//                int32_t ttt = (data & 0x00000FFF00000000) >> 36;
//                int32_t i = (data & 0x000000000F000000) >> 24;
//                int32_t xxx = (data & 0x0000000000FFF000) >> 12;
//                int32_t ddd = (data & 0x0000000000000FFF);
//
//                Gfx value = {gsDPLoadBlock(i, sss, ttt, xxx, ddd)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_CULLDL: {
//                int32_t vvvv = (data & 0xFFFF00000000) >> 32;
//                int32_t wwww = (data & 0x0000FFFF);
//
//                Gfx value = {gsSPCullDisplayList(vvvv / 2, wwww / 2)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_RDPHALF_1: {
//                auto data2 = BSWAP64(list[1]);
//
//                if ((data2 >> 56) != G_BRANCH_Z) {
//                    uint32_t h = (data & 0xFFFFFFFF);
//
//                    Gfx value = {gsSPBranchLessZraw3(h & 0x00FFFFFF)};
//                    w0 = value.words.w0;
//                    w1 = value.words.w1;
//                } else {
//                    w0 = (G_NOOP << 24);
//                    w1 = 0;
//                }
//                break;
//            }
//            case G_RDPHALF_2: {
//                Gfx value = {gsDPWordLo(data & 0xFFFFFFFF)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_TEXRECT: {
//                int32_t xxx = (data & 0x00FFF00000000000) >> 44;
//                int32_t yyy = (data & 0x00000FFF00000000) >> 32;
//                int32_t i = (data & 0x000000000F000000) >> 24;
//                int32_t XXX = (data & 0x0000000000FFF000) >> 12;
//                int32_t YYY = (data & 0x0000000000000FFF);
//
//                Gfx value = {gsSPTextureRectangle2(XXX, YYY, xxx, yyy, i)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_BRANCH_Z: {
//                uint32_t a = (data & 0x00FFF00000000000) >> 44;
//                uint32_t b = (data & 0x00000FFF00000000) >> 32;
//                uint32_t z = (data & 0x00000000FFFFFFFF) >> 0;
//                uint32_t h = (data & 0xFFFFFFFF);
//
//                list--;
//                auto data2 = BSWAP64(list[0]);
//                list++;
//                uint32_t dListPtr = SEGMENT_OFFSET(data2);
//
//                auto dec = Companion::Instance->GetNodeByAddr(dListPtr);
//
//                Gfx value = {gsSPBranchLessZraw2(0xDEADABCD, (a / 5) | (b / 2), z)};
//                w0 = (value.words.w0 & 0x00FFFFFF) + (G_BRANCH_Z_OTR << 24);
//                w1 = value.words.w1;
//
//                writer->Write(w0);
//                writer->Write(w1);
//
//                if(dec.has_value()){
//                    uint64_t hash = CRC64(std::get<0>(dec.value()).c_str());
//                    w0 = hash >> 32;
//                    w1 = hash & 0xFFFFFFFF;
//                } else {
//                    w0 = 0;
//                    w1 = 0;
//                    SPDLOG_INFO("Warning: Could not find display list at 0x{:X}", dListPtr);
//                }
//                break;
//            }
//            case G_DL: {
//                uint32_t dListPtr = SEGMENT_OFFSET(data);
//
//                auto dec = Companion::Instance->GetNodeByAddr(dListPtr);
//
//                writer->Write(w0);
//                writer->Write(w1);
//
//                if(dec.has_value()){
//                    uint64_t hash = CRC64(std::get<0>(dec.value()).c_str());
//                    w0 = hash >> 32;
//                    w1 = hash & 0xFFFFFFFF;
//                } else {
//                    w0 = 0;
//                    w1 = 0;
//                    SPDLOG_INFO("Warning: Could not find display list at 0x{:X}", dListPtr);
//                }
//                break;
//            }
//            case G_TEXTURE: {
//                int32_t ____ = (data & 0x0000FFFF00000000) >> 32;
//                int32_t ssss = (data & 0x00000000FFFF0000) >> 16;
//                int32_t tttt = (data & 0x000000000000FFFF);
//                int32_t lll = (____ & 0x3800) >> 11;
//                int32_t ddd = (____ & 0x700) >> 8;
//                int32_t nnnnnnn = (____ & 0xFE) >> 1;
//
//                Gfx value = {gsSPTexture(ssss, tttt, lll, ddd, nnnnnnn)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_TRI1: {
//                int32_t aa = ((data & 0x00FF000000000000ULL) >> 48) / 2;
//                int32_t bb = ((data & 0x0000FF0000000000ULL) >> 40) / 2;
//                int32_t cc = ((data & 0x000000FF00000000ULL) >> 32) / 2;
//
//                Gfx test = {gsSP1Triangle(aa, bb, cc, 0)};
//                w0 = test.words.w0;
//                w1 = test.words.w1;
//                break;
//            }
//            case G_TRI2: {
//                int32_t aa = ((data & 0x00FF000000000000ULL) >> 48) / 2;
//                int32_t bb = ((data & 0x0000FF0000000000ULL) >> 40) / 2;
//                int32_t cc = ((data & 0x000000FF00000000ULL) >> 32) / 2;
//                int32_t dd = ((data & 0x00000000FF0000ULL) >> 16) / 2;
//                int32_t ee = ((data & 0x0000000000FF00ULL) >> 8) / 2;
//                int32_t ff = ((data & 0x000000000000FFULL) >> 0) / 2;
//
//                Gfx test = {gsSP2Triangles(aa, bb, cc, 0, dd, ee, ff, 0)};
//                w0 = test.words.w0;
//                w1 = test.words.w1;
//                break;
//            }
//            case G_QUAD: {
//                int32_t aa = ((data & 0x00FF000000000000ULL) >> 48) / 2;
//                int32_t bb = ((data & 0x0000FF0000000000ULL) >> 40) / 2;
//                int32_t cc = ((data & 0x000000FF00000000ULL) >> 32) / 2;
//                int32_t dd = ((data & 0x000000000000FFULL)) / 2;
//
//                Gfx test = {gsSP1Quadrangle(aa, bb, cc, dd, 0)};
//                w0 = test.words.w0;
//                w1 = test.words.w1;
//                break;
//            }
//            case G_SETPRIMCOLOR: {
//                int32_t mm = (data & 0x0000FF0000000000) >> 40;
//                int32_t ff = (data & 0x000000FF00000000) >> 32;
//                int32_t rr = (data & 0x00000000FF000000) >> 24;
//                int32_t gg = (data & 0x0000000000FF0000) >> 16;
//                int32_t bb = (data & 0x000000000000FF00) >> 8;
//                int32_t aa = (data & 0x00000000000000FF) >> 0;
//
//                Gfx value = {gsDPSetPrimColor(mm, ff, rr, gg, bb, aa)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_SETOTHERMODE_L: {
//                int32_t ss = (data & 0x0000FF0000000000) >> 40;
//                int32_t len = ((data & 0x000000FF00000000) >> 32) + 1;
//                int32_t sft = 32 - (len)-ss;
//                int32_t dd = (data & 0xFFFFFFFF);
//
//                // TODO: Output the correct render modes in data
//
//                Gfx value = {gsSPSetOtherMode(0xE2, sft, len, dd)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_SETOTHERMODE_H: {
//                int32_t ss = (data & 0x0000FF0000000000) >> 40;
//                int32_t nn = (data & 0x000000FF00000000) >> 32;
//                int32_t dd = (data & 0xFFFFFFFF);
//
//                int32_t sft = 32 - (nn + 1) - ss;
//
//                Gfx value;
//
//                if (sft == 14) {
//                    value = {gsDPSetTextureLUT(dd >> 14)};
//                } else {
//                    value = {gsSPSetOtherMode(0xE3, sft, nn + 1, dd)};
//                }
//
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_SETTILE: {
//                int32_t fff = (data & 0b0000000011100000000000000000000000000000000000000000000000000000) >> 53;
//                int32_t ii = (data & 0b0000000000011000000000000000000000000000000000000000000000000000) >> 51;
//                int32_t nnnnnnnnn =
//                        (data & 0b0000000000000011111111100000000000000000000000000000000000000000) >> 41;
//                int32_t mmmmmmmmm =
//                        (data & 0b0000000000000000000000011111111100000000000000000000000000000000) >> 32;
//                int32_t ttt = (data & 0b0000000000000000000000000000000000000111000000000000000000000000) >> 24;
//                int32_t pppp =
//                        (data & 0b0000000000000000000000000000000000000000111100000000000000000000) >> 20;
//                int32_t cc = (data & 0b0000000000000000000000000000000000000000000011000000000000000000) >> 18;
//                int32_t aaaa =
//                        (data & 0b0000000000000000000000000000000000000000000000111100000000000000) >> 14;
//                int32_t ssss =
//                        (data & 0b0000000000000000000000000000000000000000000000000011110000000000) >> 10;
//                int32_t dd = (data & 0b0000000000000000000000000000000000000000000000000000001100000000) >> 8;
//                int32_t bbbb = (data & 0b0000000000000000000000000000000000000000000000000000000011110000) >> 4;
//                int32_t uuuu = (data & 0b0000000000000000000000000000000000000000000000000000000000001111);
//
//                Gfx value = {gsDPSetTile(fff, ii, nnnnnnnnn, mmmmmmmmm, ttt, pppp, cc, aaaa, ssss, dd, bbbb, uuuu)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_SETCOMBINE: {
//                int32_t a0 = (data & 0b000000011110000000000000000000000000000000000000000000000000000) >> 52;
//                int32_t c0 = (data & 0b000000000001111100000000000000000000000000000000000000000000000) >> 47;
//                int32_t aa0 = (data & 0b00000000000000011100000000000000000000000000000000000000000000) >> 44;
//                int32_t ac0 = (data & 0b00000000000000000011100000000000000000000000000000000000000000) >> 41;
//                int32_t a1 = (data & 0b000000000000000000000011110000000000000000000000000000000000000) >> 37;
//                int32_t c1 = (data & 0b000000000000000000000000001111100000000000000000000000000000000) >> 32;
//                int32_t b0 = (data & 0b000000000000000000000000000000011110000000000000000000000000000) >> 28;
//                int32_t b1 = (data & 0b000000000000000000000000000000000001111000000000000000000000000) >> 24;
//                int32_t aa1 = (data & 0b00000000000000000000000000000000000000111000000000000000000000) >> 21;
//                int32_t ac1 = (data & 0b00000000000000000000000000000000000000000111000000000000000000) >> 18;
//                int32_t d0 = (data & 0b000000000000000000000000000000000000000000000111000000000000000) >> 15;
//                int32_t ab0 = (data & 0b00000000000000000000000000000000000000000000000111000000000000) >> 12;
//                int32_t ad0 = (data & 0b00000000000000000000000000000000000000000000000000111000000000) >> 9;
//                int32_t d1 = (data & 0b000000000000000000000000000000000000000000000000000000111000000) >> 6;
//                int32_t ab1 = (data & 0b00000000000000000000000000000000000000000000000000000000111000) >> 3;
//                int32_t ad1 = (data & 0b00000000000000000000000000000000000000000000000000000000000111) >> 0;
//
//                Gfx value = { gsDPSetCombineLERP_NoMacros(a0, b0, c0, d0, aa0, ab0, ac0, ad0, a1, b1, c1, d1, aa1, ab1, ac1, ad1)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_SETTILESIZE: {
//                int32_t sss = (data & 0x00FFF00000000000) >> 44;
//                int32_t ttt = (data & 0x00000FFF00000000) >> 32;
//                int32_t uuu = (data & 0x0000000000FFF000) >> 12;
//                int32_t vvv = (data & 0x0000000000000FFF);
//                int32_t i = (data & 0x000000000F000000) >> 24;
//
//                Gfx value = {gsDPSetTileSize(i, sss, ttt, uuu, vvv)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_LOADTLUT: {
//                int32_t t = (data & 0x0000000007000000) >> 24;
//                int32_t ccc = (data & 0x00000000003FF000) >> 14;
//
//                Gfx value = {gsDPLoadTLUTCmd(t, ccc)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_LOADTILE: {
//                int sss =	(data & 0x00FFF00000000000) >> 44;
//                int ttt =	(data & 0x00000FFF00000000) >> 32;
//                int i =		(data & 0x000000000F000000) >> 24;
//                int uuu =	(data & 0x0000000000FFF000) >> 12;
//                int vvv=	(data & 0x0000000000000FFF);
//
//                Gfx value = {gsDPLoadTile(i, sss, ttt, uuu, vvv)};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                break;
//            }
//            case G_SETTIMG: {
//
//                uint32_t ptr = SEGMENT_OFFSET(w1);
//                auto decl = Companion::Instance->GetNodeByAddr(ptr);
//
//                int32_t __ = (data & 0x00FF000000000000) >> 48;
//                int32_t www = (data & 0x00000FFF00000000) >> 32;
//
//                uint32_t fmt = (__ & 0xE0) >> 5;
//                uint32_t siz = (__ & 0x18) >> 3;
//
//                Gfx value = {gsDPSetTextureImage(fmt, siz, www + 1, __)};
//                w0 = value.words.w0 & 0x00FFFFFF;
//                w0 += (G_SETTIMG_OTR_HASH << 24);
//                //word1 = value.words.w1;
//                w1 = 0;
//
//                writer->Write(w0);
//                writer->Write(w1);
//
//                if(decl.has_value()){
//                    uint64_t hash = CRC64(std::get<0>(decl.value()).c_str());
//
//                    w0 = hash >> 32;
//                    w1 = hash & 0xFFFFFFFF;
//                    SPDLOG_INFO("Found texture: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(decl.value()));
//                } else {
//                    w0 = 0;
//                    w1 = 0;
//                    SPDLOG_INFO("Warning: Could not find texture at 0x{:X}", ptr);
//                }
//                break;
//            }
//            case G_VTX: {
////                throw std::runtime_error("G_VTX not implemented");
//                break;
//            }
//            case G_ENDDL: {
//                Gfx value = {gsSPEndDisplayList()};
//                w0 = value.words.w0;
//                w1 = value.words.w1;
//                writer->Write(w0);
//                writer->Write(w1);
//                return true;
//            }
//            default:
////                std::cout << "Unknown or unimplemented opcode: " << std::hex << opcode << std::endl;
//                break;
//        }
//
//        writer->Write(w0);
//        writer->Write(w1);
//        list++;
    }
}
