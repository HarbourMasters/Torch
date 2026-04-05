#ifdef OOT_SUPPORT

#include "OoTCurveAnimationFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

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

} // namespace OoT

#endif
