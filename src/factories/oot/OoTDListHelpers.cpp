#ifdef OOT_SUPPORT

#include "OoTDListHelpers.h"
#include "Companion.h"

namespace OoT {
namespace DListHelpers {

uint32_t RemapSegmentedAddr(uint32_t addr, const std::string& expectedType) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::OoT) return addr;

    uint8_t seg = SEGMENT_NUMBER(addr);
    uint32_t offset = SEGMENT_OFFSET(addr);
    auto segBase = Companion::Instance->GetFileOffsetFromSegmentedAddr(seg);
    if (!segBase.has_value()) return addr;

    for (uint8_t otherSeg = 1; otherSeg < 0x20; otherSeg++) {
        if (otherSeg == seg) continue;
        auto otherBase = Companion::Instance->GetFileOffsetFromSegmentedAddr(otherSeg);
        if (otherBase.has_value() && otherBase.value() == segBase.value()) {
            uint32_t remapped = (otherSeg << 24) | offset;
            auto node = Companion::Instance->GetNodeByAddr(remapped);
            if (node.has_value()) {
                if (!expectedType.empty()) {
                    auto n = std::get<1>(node.value());
                    auto nType = GetSafeNode<std::string>(n, "type");
                    if (nType != expectedType) continue;
                }
                return remapped;
            }
        }
    }
    return addr;
}

std::optional<std::tuple<std::string, YAML::Node>> SearchVtx(uint32_t ptr) {
    if (Companion::Instance->GetGBIMinorVersion() != GBIMinorVersion::OoT) return std::nullopt;

    std::vector<std::string> vtxTypes = {"VTX", "OOT:ARRAY"};

    uint32_t absPtr = ptr;
    if (IS_SEGMENTED(ptr)) {
        auto seg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(ptr));
        if (!seg.has_value()) {
            return std::nullopt;
        }
        absPtr = seg.value() + SEGMENT_OFFSET(ptr);
    }

    for (const auto& type : vtxTypes) {
        auto decs = Companion::Instance->GetNodesByType(type);
        if (!decs.has_value()) continue;

        for (auto& dec : decs.value()) {
            auto [name, node] = dec;

            if (type == "OOT:ARRAY") {
                auto arrayType = GetSafeNode<std::string>(node, "array_type", "");
                if (arrayType != "VTX") continue;
            }

            auto offset = GetSafeNode<uint32_t>(node, "offset");
            auto count = GetSafeNode<uint32_t>(node, "count");
            auto end = ((count * 16 + 0xF) & ~0xF); // ALIGN16(count * sizeof(N64Vtx_t))

            uint32_t absOffset = offset;
            if (IS_SEGMENTED(offset)) {
                auto seg = Companion::Instance->GetFileOffsetFromSegmentedAddr(SEGMENT_NUMBER(offset));
                if (!seg.has_value()) continue;
                absOffset = seg.value() + SEGMENT_OFFSET(offset);
            }

            if (absPtr > absOffset && absPtr < absOffset + end) {
                return std::make_tuple(GetSafeNode<std::string>(node, "symbol", name), node);
            }
        }
    }

    return std::nullopt;
}

} // namespace DListHelpers
} // namespace OoT

#endif
