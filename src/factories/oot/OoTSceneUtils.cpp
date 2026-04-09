#ifdef OOT_SUPPORT

#include "OoTSceneUtils.h"

namespace OoT {

LUS::BinaryReader ReadSubArray(std::vector<uint8_t>& buffer, uint32_t segAddr, uint32_t size) {
    YAML::Node node;
    node["offset"] = Companion::Instance->PatchVirtualAddr(segAddr);
    auto raw = Decompressor::AutoDecode(node, buffer, size);
    LUS::BinaryReader reader(raw.segment.data, raw.segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    return reader;
}

std::string ResolvePointer(uint32_t ptr) {
    if (ptr == 0) return "";
    ptr = Companion::Instance->PatchVirtualAddr(ptr);
    auto result = Companion::Instance->GetStringByAddr(ptr);
    if (result.has_value()) return result.value();
    return "";
}

std::string MakeAssetName(const std::string& baseName, const std::string& suffix, uint32_t offset) {
    std::ostringstream ss;
    ss << baseName << suffix << "_" << std::uppercase << std::hex
       << std::setfill('0') << std::setw(6) << offset;
    return ss.str();
}

std::vector<char> SerializePathways(std::vector<uint8_t>& buffer,
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

} // namespace OoT

#endif
