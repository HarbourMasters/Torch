#include "MMUtils.h"

#include "Companion.h"
#include "utils/Decompressor.h"

namespace MM {

LUS::BinaryReader ReadSubArray(std::vector<uint8_t>& buffer, uint32_t segmentedAddress, uint32_t size) {
    YAML::Node node;
    node["offset"] = Companion::Instance->PatchVirtualAddr(segmentedAddress);
    const auto raw = Decompressor::AutoDecode(node, buffer, size);
    LUS::BinaryReader reader(raw.segment.data, raw.segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    return reader;
}

} // namespace MM
