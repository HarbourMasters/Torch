#pragma once

#include <cstdint>
#include "DisplayListFactory.h"

namespace GFXDOverride {
void Quadrangle(const Gfx* gfx);
void Triangle2(const Gfx* gfx);
int  Vtx(uint32_t vtx, int32_t num);
int  Texture(uint32_t timg, int32_t fmt, int32_t siz, int32_t width, int32_t height, int32_t pal);
int  Light(uint32_t lightsn, int32_t count);
int  DisplayList(uint32_t dl);
};