#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <string>
#include <optional>
#include <tuple>

namespace OoT {
namespace DListHelpers {

// Remap a segmented address to an alias segment where the asset is registered.
uint32_t RemapSegmentedAddr(uint32_t addr, const std::string& expectedType = "");

} // namespace DListHelpers
} // namespace OoT

#endif
