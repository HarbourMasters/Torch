#ifdef OOT_SUPPORT

#include "OoTSkeletonTypes.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

namespace OoT {

OoTLimbType ParseLimbType(const std::string& str) {
    if (str == "Standard") return OoTLimbType::Standard;
    if (str == "LOD") return OoTLimbType::LOD;
    if (str == "Skin") return OoTLimbType::Skin;
    if (str == "Curve") return OoTLimbType::Curve;
    if (str == "Legacy") return OoTLimbType::Legacy;
    SPDLOG_ERROR("Unknown OoT limb type '{}'", str);
    return OoTLimbType::Invalid;
}

OoTSkeletonType ParseSkeletonType(const std::string& str) {
    if (str == "Normal") return OoTSkeletonType::Normal;
    if (str == "Flex") return OoTSkeletonType::Flex;
    if (str == "Curve") return OoTSkeletonType::Curve;
    SPDLOG_ERROR("Unknown OoT skeleton type '{}'", str);
    return OoTSkeletonType::Normal;
}

std::string ResolveGfxPointer(uint32_t ptr, const std::string& limbSymbol,
                              const std::string& suffix, bool autoDiscover) {
    if (ptr == 0) return "";
    ptr = Companion::Instance->PatchVirtualAddr(ptr);
    auto result = Companion::Instance->GetStringByAddr(ptr);
    if (result.has_value()) {
        return result.value();
    }

    SPDLOG_WARN("Could not resolve GFX pointer 0x{:08X} — YAML enrichment incomplete", ptr);
    return "";
}

} // namespace OoT

#endif
