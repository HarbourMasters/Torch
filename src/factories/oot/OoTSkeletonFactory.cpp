#ifdef OOT_SUPPORT

#include "OoTSkeletonFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

#include <sstream>
#include <iomanip>

namespace OoT {

static OoTLimbType ParseLimbType(const std::string& str) {
    if (str == "Standard") return OoTLimbType::Standard;
    if (str == "LOD") return OoTLimbType::LOD;
    if (str == "Skin") return OoTLimbType::Skin;
    if (str == "Curve") return OoTLimbType::Curve;
    if (str == "Legacy") return OoTLimbType::Legacy;
    SPDLOG_ERROR("Unknown OoT limb type '{}'", str);
    return OoTLimbType::Invalid;
}

static OoTSkeletonType ParseSkeletonType(const std::string& str) {
    if (str == "Normal") return OoTSkeletonType::Normal;
    if (str == "Flex") return OoTSkeletonType::Flex;
    if (str == "Curve") return OoTSkeletonType::Curve;
    SPDLOG_ERROR("Unknown OoT skeleton type '{}'", str);
    return OoTSkeletonType::Normal;
}

static std::string ResolvePointer(uint32_t ptr) {
    if (ptr == 0) return "";
    // PatchVirtualAddr handles overlay virtual addresses (0x80xxxxxx -> segmented)
    ptr = Companion::Instance->PatchVirtualAddr(ptr);
    auto result = Companion::Instance->GetStringByAddr(ptr);
    if (result.has_value()) {
        return result.value();
    }
    SPDLOG_WARN("Could not resolve pointer 0x{:08X}", ptr);
    return "";
}

// Resolve a DList pointer, auto-discovering GFX assets when not found in YAML
// When autoDiscover is false, only resolves existing assets without creating new ones
static std::string ResolveGfxPointer(uint32_t ptr, const std::string& limbSymbol, const std::string& suffix, bool autoDiscover = true) {
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

    // Auto-discover: create a GFX entry for this DList
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

// ==================== OoT Limb Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTLimbFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto limbTypeStr = GetSafeNode<std::string>(node, "limb_type");
    auto limbType = ParseLimbType(limbTypeStr);
    if (limbType == OoTLimbType::Invalid) {
        return std::nullopt;
    }

    size_t dataSize;
    switch (limbType) {
        case OoTLimbType::Standard:
        case OoTLimbType::Curve:
            dataSize = 0x0C;
            break;
        case OoTLimbType::LOD:
        case OoTLimbType::Skin:
            dataSize = 0x10;
            break;
        case OoTLimbType::Legacy:
            dataSize = 0x20;
            break;
        default:
            dataSize = 0x0C;
            break;
    }

    auto [_, segment] = Decompressor::AutoDecode(node, buffer, dataSize);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto limb = std::make_shared<OoTLimbData>();
    limb->limbType = limbType;
    auto symbol = GetSafeNode<std::string>(node, "symbol");
    // Auto-discovered limbs (created by skeleton factory) should not auto-discover GFX
    // because the GFX processing may reference unconfigured segments and crash
    bool canAutoDiscoverGfx = !node["auto_discovered"].IsDefined();

    if (limbType == OoTLimbType::Curve) {
        limb->childIndex = reader.ReadUByte();
        limb->siblingIndex = reader.ReadUByte();
        reader.ReadUInt16(); // padding
        uint32_t dListAddr = reader.ReadUInt32();
        uint32_t dList2Addr = reader.ReadUInt32();
        limb->dListPtr = ResolveGfxPointer(dListAddr, symbol, "DL", canAutoDiscoverGfx);
        limb->dList2Ptr = ResolveGfxPointer(dList2Addr, symbol, "DL2", canAutoDiscoverGfx);
    } else if (limbType == OoTLimbType::Legacy) {
        uint32_t dListAddr = reader.ReadUInt32();
        limb->dListPtr = ResolveGfxPointer(dListAddr, symbol, "DL", canAutoDiscoverGfx);
        limb->legTransX = reader.ReadFloat();
        limb->legTransY = reader.ReadFloat();
        limb->legTransZ = reader.ReadFloat();
        limb->rotX = reader.ReadUInt16();
        limb->rotY = reader.ReadUInt16();
        limb->rotZ = reader.ReadUInt16();
        reader.ReadUInt16(); // padding
        uint32_t childAddr = reader.ReadUInt32();
        uint32_t siblingAddr = reader.ReadUInt32();
        limb->childPtr = ResolvePointer(childAddr);
        limb->siblingPtr = ResolvePointer(siblingAddr);
    } else {
        // Standard, LOD, Skin
        limb->transX = reader.ReadInt16();
        limb->transY = reader.ReadInt16();
        limb->transZ = reader.ReadInt16();
        limb->childIndex = reader.ReadUByte();
        limb->siblingIndex = reader.ReadUByte();

        if (limbType == OoTLimbType::Standard) {
            uint32_t dListAddr = reader.ReadUInt32();
            limb->dListPtr = ResolveGfxPointer(dListAddr, symbol, "DL", canAutoDiscoverGfx);
        } else if (limbType == OoTLimbType::LOD) {
            uint32_t dListAddr = reader.ReadUInt32();
            uint32_t dList2Addr = reader.ReadUInt32();
            limb->dListPtr = ResolveGfxPointer(dListAddr, symbol, "DL", canAutoDiscoverGfx);
            limb->dList2Ptr = ResolveGfxPointer(dList2Addr, symbol, "DL2", canAutoDiscoverGfx);
        } else if (limbType == OoTLimbType::Skin) {
            limb->skinSegmentType = static_cast<OoTLimbSkinType>(reader.ReadInt32());
            uint32_t skinSegmentAddr = reader.ReadUInt32();

            skinSegmentAddr = Companion::Instance->PatchVirtualAddr(skinSegmentAddr);
            if (limb->skinSegmentType == OoTLimbSkinType::SkinType_Normal) {
                limb->skinDList = ResolveGfxPointer(skinSegmentAddr, symbol, "DL", canAutoDiscoverGfx);
            } else if (limb->skinSegmentType == OoTLimbSkinType::SkinType_Animated && skinSegmentAddr != 0) {
                // Read the SkinAnimatedLimbData struct
                YAML::Node skinNode;
                skinNode["offset"] = skinSegmentAddr;
                auto skinRaw = Decompressor::AutoDecode(skinNode, buffer, 0x0C);
                LUS::BinaryReader skinReader(skinRaw.segment.data, skinRaw.segment.size);
                skinReader.SetEndianness(Torch::Endianness::Big);

                limb->skinAnimData.totalVtxCount = skinReader.ReadUInt16();
                uint16_t limbModifCount = skinReader.ReadUInt16();
                uint32_t limbModifAddr = Companion::Instance->PatchVirtualAddr(skinReader.ReadUInt32());
                uint32_t skinDListAddr = Companion::Instance->PatchVirtualAddr(skinReader.ReadUInt32());

                limb->skinVtxCnt = limb->skinAnimData.totalVtxCount;
                limb->skinAnimData.dlist = ResolveGfxPointer(skinDListAddr, symbol, "SkinLimbDL", canAutoDiscoverGfx);

                // Read each SkinLimbModif (0x10 bytes each)
                if (limbModifAddr != 0 && limbModifCount > 0) {
                    YAML::Node modifNode;
                    modifNode["offset"] = limbModifAddr;
                    auto modifRaw = Decompressor::AutoDecode(modifNode, buffer, limbModifCount * 0x10);
                    LUS::BinaryReader modifReader(modifRaw.segment.data, modifRaw.segment.size);
                    modifReader.SetEndianness(Torch::Endianness::Big);

                    for (uint16_t m = 0; m < limbModifCount; m++) {
                        OoTSkinLimbModif modif;
                        uint16_t vtxCount = modifReader.ReadUInt16();
                        uint16_t transformCount = modifReader.ReadUInt16();
                        modif.unk_4 = modifReader.ReadUInt16();
                        modifReader.ReadUInt16(); // padding
                        uint32_t skinVerticesAddr = Companion::Instance->PatchVirtualAddr(modifReader.ReadUInt32());
                        uint32_t limbTransAddr = Companion::Instance->PatchVirtualAddr(modifReader.ReadUInt32());

                        // Read skin vertices (0x0A bytes each)
                        if (skinVerticesAddr != 0 && vtxCount > 0) {
                            YAML::Node vtxNode;
                            vtxNode["offset"] = skinVerticesAddr;
                            auto vtxRaw = Decompressor::AutoDecode(vtxNode, buffer, vtxCount * 0x0A);
                            LUS::BinaryReader vtxReader(vtxRaw.segment.data, vtxRaw.segment.size);
                            vtxReader.SetEndianness(Torch::Endianness::Big);

                            for (uint16_t v = 0; v < vtxCount; v++) {
                                OoTSkinVertex sv;
                                sv.index = vtxReader.ReadUInt16();
                                sv.s = vtxReader.ReadInt16();
                                sv.t = vtxReader.ReadInt16();
                                sv.normX = vtxReader.ReadInt8();
                                sv.normY = vtxReader.ReadInt8();
                                sv.normZ = vtxReader.ReadInt8();
                                sv.alpha = vtxReader.ReadUByte();
                                modif.skinVertices.push_back(sv);
                            }
                        }

                        // Read skin transformations (0x0A bytes each)
                        if (limbTransAddr != 0 && transformCount > 0) {
                            YAML::Node transNode;
                            transNode["offset"] = limbTransAddr;
                            auto transRaw = Decompressor::AutoDecode(transNode, buffer, transformCount * 0x0A);
                            LUS::BinaryReader transReader(transRaw.segment.data, transRaw.segment.size);
                            transReader.SetEndianness(Torch::Endianness::Big);

                            for (uint16_t t = 0; t < transformCount; t++) {
                                OoTSkinTransformation st;
                                st.limbIndex = transReader.ReadUByte();
                                transReader.ReadUByte(); // padding
                                st.x = transReader.ReadInt16();
                                st.y = transReader.ReadInt16();
                                st.z = transReader.ReadInt16();
                                st.scale = transReader.ReadUByte();
                                transReader.ReadUByte(); // padding
                                modif.limbTransformations.push_back(st);
                            }
                        }

                        limb->skinAnimData.limbModifications.push_back(modif);
                    }
                }
            }
        }
    }

    return limb;
}

ExportResult OoTLimbBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node,
                                           std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto limb = std::static_pointer_cast<OoTLimbData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTSkeletonLimb, 0);

    // limbType
    writer.Write(static_cast<uint8_t>(limb->limbType));
    // skinSegmentType
    writer.Write(static_cast<uint8_t>(limb->skinSegmentType));
    // skinDList (string)
    writer.Write(limb->skinDList);
    // skinVtxCnt
    writer.Write(limb->skinVtxCnt);
    // skinLimbModifCount
    writer.Write(static_cast<uint32_t>(limb->skinAnimData.limbModifications.size()));

    for (auto& modif : limb->skinAnimData.limbModifications) {
        writer.Write(modif.unk_4);

        writer.Write(static_cast<uint32_t>(modif.skinVertices.size()));
        for (auto& sv : modif.skinVertices) {
            writer.Write(sv.index);
            writer.Write(sv.s);
            writer.Write(sv.t);
            writer.Write(sv.normX);
            writer.Write(sv.normY);
            writer.Write(sv.normZ);
            writer.Write(sv.alpha);
        }

        writer.Write(static_cast<uint32_t>(modif.limbTransformations.size()));
        for (auto& st : modif.limbTransformations) {
            writer.Write(st.limbIndex);
            writer.Write(st.x);
            writer.Write(st.y);
            writer.Write(st.z);
            writer.Write(st.scale);
        }
    }

    // skinDList2 (SkinAnimatedLimbData's dlist)
    writer.Write(limb->skinAnimData.dlist);

    // legTransX/Y/Z
    writer.Write(limb->legTransX);
    writer.Write(limb->legTransY);
    writer.Write(limb->legTransZ);

    // rotX/Y/Z
    writer.Write(limb->rotX);
    writer.Write(limb->rotY);
    writer.Write(limb->rotZ);

    // childPtr (string, Legacy only)
    writer.Write(limb->childPtr);
    // siblingPtr (string, Legacy only)
    writer.Write(limb->siblingPtr);
    // dListPtr (string)
    writer.Write(limb->dListPtr);
    // dList2Ptr (string, LOD/Curve only)
    writer.Write(limb->dList2Ptr);

    // transX/Y/Z
    writer.Write(limb->transX);
    writer.Write(limb->transY);
    writer.Write(limb->transZ);

    // childIndex, siblingIndex
    writer.Write(limb->childIndex);
    writer.Write(limb->siblingIndex);

    writer.Finish(write);
    return std::nullopt;
}

// ==================== OoT Skeleton Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTSkeletonFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto skelTypeStr = GetSafeNode<std::string>(node, "skel_type");
    auto limbTypeStr = GetSafeNode<std::string>(node, "limb_type");
    auto skelType = ParseSkeletonType(skelTypeStr);
    auto limbType = ParseLimbType(limbTypeStr);

    // Skeleton ROM layout:
    // +0x00: segptr_t limbsArrayAddress (4 bytes)
    // +0x04: uint8_t limbCount (1 byte)
    // For Flex: +0x08: uint8_t dListCount (1 byte)
    size_t skelSize = (skelType == OoTSkeletonType::Flex) ? 0x0C : 0x08;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, skelSize);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    uint32_t limbsArrayAddr = reader.ReadUInt32();
    // Patch virtual address for overlays (e.g. 0x80B65CC4 -> 0x06001BA4)
    limbsArrayAddr = Companion::Instance->PatchVirtualAddr(limbsArrayAddr);
    uint8_t limbCount = reader.ReadUByte();
    uint8_t dListCount = 0;

    if (skelType == OoTSkeletonType::Flex) {
        reader.Seek(8, LUS::SeekOffsetType::Start);
        dListCount = reader.ReadUByte();
    }

    // Read the limb table (array of segmented pointers)
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
                // Auto-create limb entry (matches ZAPD naming: {skelName}LimbsLimb_{offset:06X})
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

    // type
    writer.Write(static_cast<uint8_t>(skel->skelType));
    // limbType
    writer.Write(static_cast<uint8_t>(skel->limbType));
    // limbCount
    writer.Write(static_cast<uint32_t>(skel->limbCount));
    // dListCount
    writer.Write(static_cast<uint32_t>(skel->dListCount));
    // limbTableType (same as limbType)
    writer.Write(static_cast<uint8_t>(skel->limbType));
    // limbTableCount (same as limbCount)
    writer.Write(static_cast<uint32_t>(skel->limbPaths.size()));

    // Limb paths
    for (auto& path : skel->limbPaths) {
        writer.Write(path);
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
