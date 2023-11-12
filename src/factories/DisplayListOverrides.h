#pragma once

#include <cstdint>

namespace GFXDOverride {
void Quadrangle();
int  Vtx(uint32_t vtx, int32_t num);
int  Texture(uint32_t timg, int32_t fmt, int32_t siz, int32_t width, int32_t height, int32_t pal);
int  DisplayList(uint32_t dl);
};