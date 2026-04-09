#pragma once

#ifdef OOT_SUPPORT

#include <cstdint>

namespace OoT {

enum class OoTAnimationType : uint32_t {
    Normal = 0,
    Link = 1,
    Curve = 2,
    Legacy = 3,
};

struct RotationIndex {
    uint16_t x, y, z;
};

struct CurveInterpKnot {
    uint16_t unk_00;
    int16_t unk_02;
    int16_t unk_04;
    int16_t unk_06;
    float unk_08;
};

} // namespace OoT

#endif
