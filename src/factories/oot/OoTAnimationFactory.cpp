#ifdef OOT_SUPPORT

#include "OoTAnimationFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

// ==================== OoT Normal Animation Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTAnimationFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // Check for legacy animation type (data stays in ROM segment, not extracted)
    auto animType = GetSafeNode<std::string>(node, "anim_type", "normal");
    if (animType == "legacy") {
        auto anim = std::make_shared<OoTNormalAnimationData>();
        anim->frameCount = 0;
        anim->limit = 0;
        anim->isLegacy = true;
        return anim;
    }

    // ROM layout: AnimationHeader (16 bytes)
    // +0x00: int16 frameCount
    // +0x02: int16 padding
    // +0x04: segptr rotationValues
    // +0x08: segptr rotationIndices
    // +0x0C: int16 limit
    // +0x0E: int16 padding
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x10);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto anim = std::make_shared<OoTNormalAnimationData>();
    anim->frameCount = reader.ReadInt16();
    reader.ReadInt16(); // padding
    uint32_t rawRotValues = reader.ReadUInt32();
    uint32_t rawRotIndices = reader.ReadUInt32();
    SPDLOG_WARN("Animation raw pointers: rotValues=0x{:08X} rotIndices=0x{:08X}", rawRotValues, rawRotIndices);
    // Log segment map
    for (auto& [seg, off] : Companion::Instance->GetConfig().segment.local) {
        SPDLOG_WARN("  segment[{}] = 0x{:X}", seg, off);
    }
    uint32_t rotValuesAddr = Companion::Instance->PatchVirtualAddr(rawRotValues);
    uint32_t rotIndicesAddr = Companion::Instance->PatchVirtualAddr(rawRotIndices);
    SPDLOG_WARN("Animation patched: rotValues=0x{:08X} rotIndices=0x{:08X}", rotValuesAddr, rotIndicesAddr);
    anim->limit = reader.ReadInt16();

    // Translate segmented addresses to file offsets
    uint32_t rotValuesOffset = Decompressor::TranslateAddr(rotValuesAddr);
    uint32_t rotIndicesOffset = Decompressor::TranslateAddr(rotIndicesAddr);
    uint32_t animHeaderOffset = Decompressor::TranslateAddr(Companion::Instance->PatchVirtualAddr(GetSafeNode<uint32_t>(node, "offset")));

    // Read rotation values: array of uint16 from rotValues to rotIndices
    uint32_t rotValuesCount = (rotIndicesOffset - rotValuesOffset) / 2;
    if (rotValuesCount > 0 && rotValuesOffset < rotIndicesOffset) {
        YAML::Node rvNode;
        rvNode["offset"] = rotValuesAddr;
        auto rvRaw = Decompressor::AutoDecode(rvNode, buffer, rotValuesCount * 2);
        LUS::BinaryReader rvReader(rvRaw.segment.data, rvRaw.segment.size);
        rvReader.SetEndianness(Torch::Endianness::Big);

        for (uint32_t i = 0; i < rotValuesCount; i++) {
            anim->rotationValues.push_back(rvReader.ReadUInt16());
        }
    }

    // Read rotation indices: array of {x,y,z} uint16 from rotIndices to animHeader
    uint32_t rotIndicesCount = (animHeaderOffset - rotIndicesOffset) / 6;
    if (rotIndicesCount > 0 && rotIndicesOffset < animHeaderOffset) {
        YAML::Node riNode;
        riNode["offset"] = rotIndicesAddr;
        auto riRaw = Decompressor::AutoDecode(riNode, buffer, rotIndicesCount * 6);
        LUS::BinaryReader riReader(riRaw.segment.data, riRaw.segment.size);
        riReader.SetEndianness(Torch::Endianness::Big);

        for (uint32_t i = 0; i < rotIndicesCount; i++) {
            RotationIndex ri;
            ri.x = riReader.ReadUInt16();
            ri.y = riReader.ReadUInt16();
            ri.z = riReader.ReadUInt16();
            anim->rotationIndices.push_back(ri);
        }
    }

    return anim;
}

ExportResult OoTAnimationBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                 std::string& entryName, YAML::Node& node,
                                                 std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto anim = std::static_pointer_cast<OoTNormalAnimationData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTAnimation, 0);

    if (anim->isLegacy) {
        writer.Write(static_cast<uint32_t>(OoTAnimationType::Legacy));
        writer.Finish(write);
        return std::nullopt;
    }

    writer.Write(static_cast<uint32_t>(OoTAnimationType::Normal));
    writer.Write(anim->frameCount);

    writer.Write(static_cast<uint32_t>(anim->rotationValues.size()));
    for (auto& val : anim->rotationValues) {
        writer.Write(val);
    }

    writer.Write(static_cast<uint32_t>(anim->rotationIndices.size()));
    for (auto& ri : anim->rotationIndices) {
        writer.Write(ri.x);
        writer.Write(ri.y);
        writer.Write(ri.z);
    }

    writer.Write(anim->limit);

    writer.Finish(write);
    return std::nullopt;
}

// ==================== OoT Curve Animation Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTCurveAnimationFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // ROM layout: CurveAnimationHeader (16 bytes)
    // +0x00: segptr refIndex
    // +0x04: segptr transformData
    // +0x08: segptr copyValues
    // +0x0C: int16 unk_0C
    // +0x0E: int16 unk_10
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x10);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    // ZAnimation base class reads frameCount from offset 0 (which for curve anims
    // is actually the high 16 bits of refIndex pointer — matches OTRExporter behavior)
    auto anim = std::make_shared<OoTCurveAnimationData>();
    reader.Seek(0, LUS::SeekOffsetType::Start);
    anim->frameCount = reader.ReadInt16();
    reader.Seek(0, LUS::SeekOffsetType::Start);

    uint32_t refIndexAddr = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());
    uint32_t transformDataAddr = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());
    uint32_t copyValuesAddr = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());

    // Get limb count from the skeleton referenced by skel_offset
    // skel_offset is segment-relative, so construct full segmented address
    // using the same segment as this animation
    uint32_t animOffset = GetSafeNode<uint32_t>(node, "offset");
    uint8_t segNum = SEGMENT_NUMBER(animOffset);
    uint32_t skelOffset = (segNum << 24) | GetSafeNode<uint32_t>(node, "skel_offset");
    YAML::Node skelNode;
    skelNode["offset"] = skelOffset;
    auto skelRaw = Decompressor::AutoDecode(skelNode, buffer, 0x08);
    LUS::BinaryReader skelReader(skelRaw.segment.data, skelRaw.segment.size);
    skelReader.SetEndianness(Torch::Endianness::Big);
    skelReader.ReadUInt32(); // skip limbs array ptr
    uint8_t limbCount = skelReader.ReadUByte();

    // Read refIndex array: 3 * 3 * limbCount entries of uint8
    size_t transformDataSize = 0;
    size_t copyValuesSize = 0;
    if (refIndexAddr != 0) {
        uint32_t refCount = 3 * 3 * limbCount;
        YAML::Node riNode;
        riNode["offset"] = refIndexAddr;
        auto riRaw = Decompressor::AutoDecode(riNode, buffer, refCount);
        LUS::BinaryReader riReader(riRaw.segment.data, riRaw.segment.size);

        for (uint32_t i = 0; i < refCount; i++) {
            uint8_t ref = riReader.ReadUByte();
            if (ref == 0) {
                copyValuesSize++;
            } else {
                transformDataSize += ref;
            }
            anim->refIndexArr.push_back(ref);
        }
    }

    // Read transform data array
    if (transformDataAddr != 0 && transformDataSize > 0) {
        YAML::Node tdNode;
        tdNode["offset"] = transformDataAddr;
        auto tdRaw = Decompressor::AutoDecode(tdNode, buffer, transformDataSize * 0x0C);
        LUS::BinaryReader tdReader(tdRaw.segment.data, tdRaw.segment.size);
        tdReader.SetEndianness(Torch::Endianness::Big);

        for (size_t i = 0; i < transformDataSize; i++) {
            CurveInterpKnot knot;
            knot.unk_00 = tdReader.ReadUInt16();
            knot.unk_02 = tdReader.ReadInt16();
            knot.unk_04 = tdReader.ReadInt16();
            knot.unk_06 = tdReader.ReadInt16();
            knot.unk_08 = tdReader.ReadFloat();
            anim->transformDataArr.push_back(knot);
        }
    }

    // Read copy values array
    if (copyValuesAddr != 0 && copyValuesSize > 0) {
        YAML::Node cvNode;
        cvNode["offset"] = copyValuesAddr;
        auto cvRaw = Decompressor::AutoDecode(cvNode, buffer, copyValuesSize * 2);
        LUS::BinaryReader cvReader(cvRaw.segment.data, cvRaw.segment.size);
        cvReader.SetEndianness(Torch::Endianness::Big);

        for (size_t i = 0; i < copyValuesSize; i++) {
            anim->copyValuesArr.push_back(cvReader.ReadInt16());
        }
    }

    return anim;
}

ExportResult OoTCurveAnimationBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                      std::string& entryName, YAML::Node& node,
                                                      std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto anim = std::static_pointer_cast<OoTCurveAnimationData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTAnimation, 0);

    writer.Write(static_cast<uint32_t>(OoTAnimationType::Curve));
    writer.Write(anim->frameCount);

    writer.Write(static_cast<uint32_t>(anim->refIndexArr.size()));
    for (auto& val : anim->refIndexArr) {
        writer.Write(val);
    }

    writer.Write(static_cast<uint32_t>(anim->transformDataArr.size()));
    for (auto& knot : anim->transformDataArr) {
        writer.Write(knot.unk_00);
        writer.Write(knot.unk_02);
        writer.Write(knot.unk_04);
        writer.Write(knot.unk_06);
        writer.Write(knot.unk_08);
    }

    writer.Write(static_cast<uint32_t>(anim->copyValuesArr.size()));
    for (auto& val : anim->copyValuesArr) {
        writer.Write(val);
    }

    writer.Finish(write);
    return std::nullopt;
}

// ==================== OoT Player Animation Header Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTPlayerAnimationHeaderFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // ROM layout: PlayerAnimationHeader (8 bytes)
    // +0x00: int16 frameCount
    // +0x02: int16 padding
    // +0x04: uint32 segPtr (segment 7 pointer to link_animetion data)
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x08);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto anim = std::make_shared<OoTPlayerAnimationHeaderData>();
    anim->frameCount = reader.ReadInt16();
    reader.ReadInt16(); // padding
    uint32_t segPtr = reader.ReadUInt32();

    // Resolve the segment pointer to a path in link_animetion
    auto path = Companion::Instance->GetStringByAddr(segPtr);
    if (path.has_value()) {
        anim->animDataPath = path.value();
    } else {
        SPDLOG_WARN("PlayerAnimation: Could not resolve segment pointer 0x{:08X}", segPtr);
        anim->animDataPath = "";
    }

    return anim;
}

ExportResult OoTPlayerAnimationHeaderBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                             std::string& entryName, YAML::Node& node,
                                                             std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto anim = std::static_pointer_cast<OoTPlayerAnimationHeaderData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTAnimation, 0);

    writer.Write(static_cast<uint32_t>(OoTAnimationType::Link));
    writer.Write(anim->frameCount);
    writer.Write("__OTR__" + anim->animDataPath);

    writer.Finish(write);
    return std::nullopt;
}

// ==================== OoT Player Animation Data Factory ====================

std::optional<std::shared_ptr<IParsedData>> OoTPlayerAnimationDataFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // Player animation data is a flat array of int16 limb rotation values.
    // Size = (6 * 22 + 2) * frameCount bytes = 134 bytes per frame.
    int32_t frameCount = GetSafeNode<int32_t>(node, "frame_count");
    uint32_t dataSize = (6 * 22 + 2) * frameCount;

    auto [_, segment] = Decompressor::AutoDecode(node, buffer, dataSize);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto anim = std::make_shared<OoTPlayerAnimationData>();
    uint32_t numEntries = dataSize / 2;
    anim->limbRotData.reserve(numEntries);
    for (uint32_t i = 0; i < numEntries; i++) {
        anim->limbRotData.push_back(reader.ReadInt16());
    }

    return anim;
}

ExportResult OoTPlayerAnimationDataBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                           std::string& entryName, YAML::Node& node,
                                                           std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto anim = std::static_pointer_cast<OoTPlayerAnimationData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTPlayerAnimation, 0);

    writer.Write(static_cast<uint32_t>(anim->limbRotData.size()));
    for (auto& val : anim->limbRotData) {
        writer.Write(val);
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
