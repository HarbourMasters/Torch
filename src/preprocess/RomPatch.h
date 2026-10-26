#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ROM transforms for the `preprocess:` jobs of a config entry, so a romhack can be extracted from the ROM its patch was
// made for: rebuild the extended base, then apply the hack's patch.
namespace RomPatch {

// SM64 US ROM extended exactly like sm64extend with its defaults (64 MB, 32 KB padding, MIO0 blocks decompressed
// into the extended region, level scripts and code repointed), which ROM Manager hacks are built on.
std::vector<uint8_t> SM64Extend(const std::vector<uint8_t>& rom);

// Applies a PPF3, IPS, BPS or xdelta (VCDIFF, no secondary compression) patch, picked by its header. Throws on a patch it can't read.
std::vector<uint8_t> Apply(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& patch);

// Rewrites the CIC-NUS-6102 header checksum.
void FixN64Checksum(std::vector<uint8_t>& rom);

} // namespace RomPatch
