#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <sstream>
#include <iomanip>

namespace OoT {

// Cutscene field packing macros (matching OTRExporter's command_macros_base.h)
inline uint32_t CS_CMD_HH(uint16_t a, uint16_t b) { return ((uint32_t)b << 16) | (uint32_t)a; }
inline uint32_t CS_CMD_BBH(int8_t a, int8_t b, int16_t c) {
    return ((uint32_t)(uint8_t)a) | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint16_t)c << 16);
}
inline uint32_t CS_CMD_HBB(uint16_t a, uint8_t b, uint8_t c) {
    return (uint32_t)a | ((uint32_t)b << 16) | ((uint32_t)c << 24);
}

// Helper to read a sub-array from ROM given a segmented pointer
inline LUS::BinaryReader ReadSubArray(std::vector<uint8_t>& buffer, uint32_t segAddr, uint32_t size) {
    YAML::Node node;
    node["offset"] = Companion::Instance->PatchVirtualAddr(segAddr);
    auto raw = Decompressor::AutoDecode(node, buffer, size);
    LUS::BinaryReader reader(raw.segment.data, raw.segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    return reader;
}

// Resolve a segmented pointer to an O2R asset path string
inline std::string ResolvePointer(uint32_t ptr) {
    if (ptr == 0) return "";
    ptr = Companion::Instance->PatchVirtualAddr(ptr);
    auto result = Companion::Instance->GetStringByAddr(ptr);
    if (result.has_value()) return result.value();
    return "";
}

// Build a scene-relative asset name from offset
inline std::string MakeAssetName(const std::string& baseName, const std::string& suffix, uint32_t offset) {
    std::ostringstream ss;
    ss << baseName << suffix << "_" << std::uppercase << std::hex
       << std::setfill('0') << std::setw(6) << offset;
    return ss.str();
}

// Serialize pathway data into OoTPath binary format.
inline std::vector<char> SerializePathways(std::vector<uint8_t>& buffer,
                                           const std::vector<std::pair<uint8_t, uint32_t>>& pathways,
                                           uint32_t writeCount, uint32_t repeats) {
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTPath, 0);
    w.Write(static_cast<uint32_t>(writeCount));
    for (uint32_t r = 0; r < repeats; r++) {
        for (auto& [np, ptsAddr] : pathways) {
            w.Write(static_cast<uint32_t>(np));
            auto ptReader = ReadSubArray(buffer, ptsAddr, np * 6);
            for (uint8_t k = 0; k < np; k++) {
                w.Write(ptReader.ReadInt16());
                w.Write(ptReader.ReadInt16());
                w.Write(ptReader.ReadInt16());
            }
        }
    }
    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    return std::vector<char>(str.begin(), str.end());
}

// Serialize cutscene data into OoTCutscene binary format.
std::vector<char> SerializeCutscene(std::vector<uint8_t>& buffer, uint32_t segAddr);

} // namespace OoT

#endif
