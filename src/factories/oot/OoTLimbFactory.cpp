#ifdef OOT_SUPPORT

#include "OoTLimbFactory.h"
#include "OoTSkeletonTypes.h"
#include "OoTSceneUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

#include <sstream>
#include <iomanip>

namespace OoT {

size_t OoTLimbFactory::GetLimbDataSize(OoTLimbType type) {
    switch (type) {
        case OoTLimbType::LOD:
        case OoTLimbType::Skin:    return 0x10;
        case OoTLimbType::Legacy:  return 0x20;
        default:                   return 0x0C;
    }
}

std::optional<std::shared_ptr<IParsedData>> OoTLimbFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto limbTypeStr = GetSafeNode<std::string>(node, "limb_type");
    auto limbType = ParseLimbType(limbTypeStr);
    if (limbType == OoTLimbType::Invalid) {
        return std::nullopt;
    }

    auto [_, segment] = Decompressor::AutoDecode(node, buffer, GetLimbDataSize(limbType));
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto limb = std::make_shared<OoTLimbData>();
    limb->limbType = limbType;
    auto symbol = GetSafeNode<std::string>(node, "symbol");

    if (limbType == OoTLimbType::Curve) {
        limb->childIndex = reader.ReadUByte();
        limb->siblingIndex = reader.ReadUByte();
        reader.ReadUInt16(); // padding
        uint32_t dListAddr = reader.ReadUInt32();
        uint32_t dList2Addr = reader.ReadUInt32();
        limb->dListPtr = ResolveGfxPointer(dListAddr, symbol, "CurveDL");
        limb->dList2Ptr = ResolveGfxPointer(dList2Addr, symbol, "Curve2DL");
    } else if (limbType == OoTLimbType::Legacy) {
        uint32_t dListAddr = reader.ReadUInt32();
        limb->dListPtr = ResolveGfxPointer(dListAddr, symbol, "DL");
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
            limb->dListPtr = ResolveGfxPointer(dListAddr, symbol, "DL");
        } else if (limbType == OoTLimbType::LOD) {
            uint32_t dListAddr = reader.ReadUInt32();
            uint32_t dList2Addr = reader.ReadUInt32();
            limb->dList2Ptr = ResolveGfxPointer(dList2Addr, symbol, "FarDL");
            limb->dListPtr = ResolveGfxPointer(dListAddr, symbol, "DL");
        } else if (limbType == OoTLimbType::Skin) {
            limb->skinSegmentType = static_cast<OoTLimbSkinType>(reader.ReadInt32());
            uint32_t skinSegmentAddr = reader.ReadUInt32();

            skinSegmentAddr = Companion::Instance->PatchVirtualAddr(skinSegmentAddr);
            if (limb->skinSegmentType == OoTLimbSkinType::SkinType_Normal) {
                limb->skinDList = ResolveGfxPointer(skinSegmentAddr, symbol, "DL");
            } else if (limb->skinSegmentType == OoTLimbSkinType::SkinType_Animated && skinSegmentAddr != 0) {
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
                limb->skinAnimData.dlist = ResolveGfxPointer(skinDListAddr, symbol, "SkinLimbDL");

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

    writer.Write(static_cast<uint8_t>(limb->limbType));
    writer.Write(static_cast<uint8_t>(limb->skinSegmentType));
    writer.Write(limb->skinDList);
    writer.Write(limb->skinVtxCnt);
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

    writer.Write(limb->skinAnimData.dlist);
    writer.Write(limb->legTransX);
    writer.Write(limb->legTransY);
    writer.Write(limb->legTransZ);
    writer.Write(limb->rotX);
    writer.Write(limb->rotY);
    writer.Write(limb->rotZ);
    writer.Write(limb->childPtr);
    writer.Write(limb->siblingPtr);
    writer.Write(limb->dListPtr);
    writer.Write(limb->dList2Ptr);
    writer.Write(limb->transX);
    writer.Write(limb->transY);
    writer.Write(limb->transZ);
    writer.Write(limb->childIndex);
    writer.Write(limb->siblingIndex);

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
