#pragma once

#include "factories/BaseFactory.h"

namespace MM {

LUS::BinaryReader ReadSubArray(std::vector<uint8_t>& buffer, uint32_t segmentedAddress, uint32_t size);

} // namespace MM
