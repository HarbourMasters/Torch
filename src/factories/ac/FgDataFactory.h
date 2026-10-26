#pragma once

#include "StructArrayFactory.h"

namespace AC {

class FgDataFactory : public StructArrayFactory {
public:
    FgDataFactory() : StructArrayFactory({ "u16", "u16*256", "u8*4" }) {}
};

} // namespace AC
