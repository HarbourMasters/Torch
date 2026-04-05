#ifdef OOT_SUPPORT

#include "OoTSkeletonFactory.h"
#include "OoTSkeletonTypes.h"
#include "OoTSceneUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

#include <sstream>
#include <iomanip>

namespace OoT {

// Shared helper implementations (declared in OoTSkeletonTypes.h)

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

    if (!autoDiscover) {
        SPDLOG_WARN("Could not resolve GFX pointer 0x{:08X}", ptr);
        return "";
    }

    uint32_t offset = SEGMENT_OFFSET(ptr);
    std::ostringstream symStream;
    symStream << limbSymbol << suffix << "_" << std::uppercase << std::hex
              << std::setfill('0') << std::setw(6) << offset;
    std::string gfxSymbol = symStream.str();

    YAML::Node gfxNode;
    gfxNode["type"] = "GFX";
    gfxNode["offset"] = ptr;
    gfxNode["symbol"] = gfxSymbol;
    try {
        auto addResult = Companion::Instance->AddAsset(gfxNode);
        if (addResult.has_value()) {
            auto resolved = Companion::Instance->GetStringByAddr(ptr);
            if (resolved.has_value()) {
                return resolved.value();
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to auto-discover GFX at 0x{:08X}: {}", ptr, e.what());
        return "";
    }

    SPDLOG_WARN("Could not resolve or auto-discover GFX at 0x{:08X}", ptr);
    return "";
}

// ==================== OoT Skeleton Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTSkeletonFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto skelTypeStr = GetSafeNode<std::string>(node, "skel_type");
    auto limbTypeStr = GetSafeNode<std::string>(node, "limb_type");
    auto skelType = ParseSkeletonType(skelTypeStr);
    auto limbType = ParseLimbType(limbTypeStr);

    size_t skelSize = (skelType == OoTSkeletonType::Flex) ? 0x0C : 0x08;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, skelSize);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    uint32_t limbsArrayAddr = reader.ReadUInt32();
    limbsArrayAddr = Companion::Instance->PatchVirtualAddr(limbsArrayAddr);
    uint8_t limbCount = reader.ReadUByte();
    uint8_t dListCount = 0;

    if (skelType == OoTSkeletonType::Flex) {
        reader.Seek(8, LUS::SeekOffsetType::Start);
        dListCount = reader.ReadUByte();
    }

    auto symbol = GetSafeNode<std::string>(node, "symbol");
    std::vector<std::string> limbPaths;
    if (limbsArrayAddr != 0 && limbCount > 0) {
        YAML::Node limbTableNode;
        limbTableNode["offset"] = limbsArrayAddr;
        auto limbTableRaw = Decompressor::AutoDecode(limbTableNode, buffer, limbCount * 4);
        LUS::BinaryReader limbTableReader(limbTableRaw.segment.data, limbTableRaw.segment.size);
        limbTableReader.SetEndianness(Torch::Endianness::Big);

        for (uint8_t i = 0; i < limbCount; i++) {
            uint32_t limbAddr = limbTableReader.ReadUInt32();
            limbAddr = Companion::Instance->PatchVirtualAddr(limbAddr);
            std::string limbPath = ResolvePointer(limbAddr);
            if (limbPath.empty() && limbAddr != 0) {
                uint32_t limbOffset = SEGMENT_OFFSET(limbAddr);
                std::ostringstream limbSymbolStream;
                limbSymbolStream << symbol << "LimbsLimb_" << std::uppercase << std::hex
                                 << std::setfill('0') << std::setw(6) << limbOffset;
                std::string limbSymbol = limbSymbolStream.str();

                YAML::Node limbNode;
                limbNode["type"] = "OOT:LIMB";
                limbNode["offset"] = limbAddr;
                limbNode["symbol"] = limbSymbol;
                limbNode["limb_type"] = limbTypeStr;
                limbNode["auto_discovered"] = true;
                try {
                    auto result = Companion::Instance->AddAsset(limbNode);
                    if (result.has_value()) {
                        limbPath = ResolvePointer(limbAddr);
                    }
                } catch (const std::exception& e) {
                    SPDLOG_WARN("Skeleton: Failed to create limb {} at 0x{:08X}: {}", i, limbAddr, e.what());
                }

                if (limbPath.empty()) {
                    SPDLOG_WARN("Skeleton: Could not create limb {} at 0x{:08X}", i, limbAddr);
                }
            }
            limbPaths.push_back(limbPath);
        }
    }

    if (limbsArrayAddr != 0) {
        std::string limbTableSymbol = symbol + "Limbs";
        YAML::Node limbTableNode;
        limbTableNode["type"] = "BLOB";
        limbTableNode["offset"] = limbsArrayAddr;
        limbTableNode["size"] = 0;
        limbTableNode["symbol"] = limbTableSymbol;
        try {
            Companion::Instance->AddAsset(limbTableNode);
        } catch (const std::exception& e) {
            SPDLOG_WARN("Skeleton: Failed to create limb table {}: {}", limbTableSymbol, e.what());
        }
    }

    auto skel = std::make_shared<OoTSkeletonData>();
    skel->skelType = skelType;
    skel->limbType = limbType;
    skel->limbCount = limbCount;
    skel->dListCount = dListCount;
    skel->limbPaths = std::move(limbPaths);

    return skel;
}

ExportResult OoTSkeletonBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                std::string& entryName, YAML::Node& node,
                                                std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto skel = std::static_pointer_cast<OoTSkeletonData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTSkeleton, 0);

    writer.Write(static_cast<uint8_t>(skel->skelType));
    writer.Write(static_cast<uint8_t>(skel->limbType));
    writer.Write(static_cast<uint32_t>(skel->limbCount));
    writer.Write(static_cast<uint32_t>(skel->dListCount));
    writer.Write(static_cast<uint8_t>(skel->limbType));
    writer.Write(static_cast<uint32_t>(skel->limbPaths.size()));

    for (auto& path : skel->limbPaths) {
        writer.Write(path);
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
