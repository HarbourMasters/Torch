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

// Handle gSunDL VTX override in Export. Returns true if handled (caller should continue).
bool HandleGSunDLVtx(uint32_t w0, uint32_t w1,
                     LUS::BinaryWriter& writer, std::string* replacement);

// OoT-specific VTX search: handles OOT:ARRAY type and cross-segment comparison.
std::optional<std::tuple<std::string, YAML::Node>> SearchVtx(uint32_t ptr);

// Handle OoT-specific G_SETTIMG export. Returns true if handled.
bool HandleExportSetTImg(uint8_t opcode, uint32_t& w0, uint32_t& w1,
                         LUS::BinaryWriter& writer, std::string* replacement);

// Handle gSunDL SETTILE/LOADBLOCK texture format fixups.
void HandleGSunDLTextureFixup(uint8_t opcode, uint32_t& w0, uint32_t& w1,
                              std::string* replacement);

// Handle OoT-specific G_MTX export. Returns true if handled.
bool HandleExportMtx(uint32_t& w0, uint32_t& w1, LUS::BinaryWriter& writer);

// Handle OoT-specific G_DL export. Returns true if handled.
bool HandleExportDL(uint32_t& w0, uint32_t& w1,
                    LUS::BinaryWriter& writer, std::string* replacement);

// Handle OoT-specific G_VTX export. Returns true if handled (caller skips main path).
// Modifies w0/w1 in place for the final write.
bool HandleExportVtx(uint32_t& w0, uint32_t& w1, uint32_t& ptr,
                     size_t nvtx, size_t didx,
                     LUS::BinaryWriter& writer, std::string* replacement);

} // namespace DListHelpers
} // namespace OoT

#endif
