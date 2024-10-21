#pragma once

#include "DisplayListFactory.h"
#include <yaml-cpp/yaml.h>
#include <cstdint>
#include <tuple>
#include <string>

typedef struct {
    uint32_t w0;
    uint32_t w1;
} Words;

typedef union {
    Words words;
    long long int	force_structure_alignment;
} Gfx;

typedef struct {
    int16_t		    ob[3];
    uint16_t	    flag;
    int16_t		    tc[2];
    unsigned char	cn[4];
} Vtx_t;

typedef struct {
    int16_t		    ob[3];
    uint16_t	    flag;
    int16_t		    tc[2];
    signed char	    n[3];
    unsigned char   a;
} Vtx_tn;

typedef union {
    Vtx_t  v;
    Vtx_tn n;
} Vtx;

namespace GFXDOverride {
void Quadrangle(const Gfx* gfx);
void Triangle2(const Gfx* gfx);
int  Vtx(uint32_t vtx, int32_t num);
int  Texture(uint32_t timg, int32_t fmt, int32_t siz, int32_t width, int32_t height, int32_t pal);
int  Palette(uint32_t tlut, int32_t idx, int32_t count);
int  Lights(uint32_t lightsn, int32_t count);
int  Light(uint32_t light);
int  DisplayList(uint32_t dl);
int  Viewport(uint32_t vp);
void RegisterVTXOverlap(uint32_t ptr, std::tuple<std::string, YAML::Node>& vtx);
std::optional<std::tuple<std::string, YAML::Node>> GetVtxOverlap(uint32_t ptr);
void ClearVtx();
};