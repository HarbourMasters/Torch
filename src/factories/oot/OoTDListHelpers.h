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

// OoT-specific VTX search: handles OOT:ARRAY type and cross-segment comparison.
std::optional<std::tuple<std::string, YAML::Node>> SearchVtx(uint32_t ptr);

} // namespace DListHelpers
} // namespace OoT

#endif
