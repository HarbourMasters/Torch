#include "MMKeyFrameFactory.h"
#include "OoTSceneUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

namespace OoT {

namespace {

// ZKeyframeSkelType
constexpr uint8_t KF_SKEL_NORMAL = 0;
constexpr uint8_t KF_SKEL_FLEX = 1;

// Limb entries are 0xC bytes for a standard skeleton and 0x8 for a flex one.
constexpr uint32_t KF_STANDARD_LIMB_SIZE = 0x0C;
constexpr uint32_t KF_FLEX_LIMB_SIZE = 0x08;

uint8_t ParseLimbType(const std::string& s) {
    return s == "Flex" ? KF_SKEL_FLEX : KF_SKEL_NORMAL;
}

// A yaml offset is file-relative; give it the file's segment so node lookups and
// AutoDecode see the same form everything else uses.
uint32_t SegmentedOffset(uint32_t offset) {
    if (IS_SEGMENTED(offset)) {
        return offset;
    }
    return (Companion::Instance->GetCurrSegmentNumber() << 24) | offset;
}

uint32_t CountSetBits(uint32_t v) {
    uint32_t n = 0;
    while (v) {
        n += v & 1;
        v >>= 1;
    }
    return n;
}

class RawData : public IParsedData {
public:
    std::vector<char> mBinary;
    explicit RawData(std::vector<char> data) : mBinary(std::move(data)) {}
};

std::vector<char> Finish(LUS::BinaryWriter& w) {
    std::stringstream ss;
    w.Finish(ss);
    auto str = ss.str();
    return std::vector<char>(str.begin(), str.end());
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> MMKeyFrameSkelFactory::parse(std::vector<uint8_t>& buffer,
                                                                        YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const uint8_t limbType = ParseLimbType(GetSafeNode<std::string>(node, "limb_type", "Normal"));

    // limbCount @0, dListCount @1, limbsPtr @4
    auto head = ReadSubArray(buffer, offset, 8);
    const uint8_t limbCount = head.ReadUByte();
    const uint8_t dListCount = head.ReadUByte();
    head.ReadUInt16();
    const uint32_t limbsPtr = head.ReadUInt32();

    const uint32_t limbSize = (limbType == KF_SKEL_FLEX) ? KF_FLEX_LIMB_SIZE : KF_STANDARD_LIMB_SIZE;
    auto limbs = ReadSubArray(buffer, limbsPtr, limbCount * limbSize);

    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::MMKeyFrameSkel, 0);
    w.Write(limbCount);
    w.Write(dListCount);
    w.Write(limbType);
    w.Write(limbCount); // limbList->numLimbs, which the skeleton's own count sets

    for (uint8_t i = 0; i < limbCount; i++) {
        const uint32_t dlist = limbs.ReadUInt32();
        const uint8_t numChildren = limbs.ReadUByte();
        const uint8_t flags = limbs.ReadUByte();

        w.Write(SEGMENT_OFFSET(dlist) != 0 ? ResolvePointer(dlist) : std::string());
        w.Write(numChildren);
        w.Write(flags);

        if (limbType == KF_SKEL_FLEX) {
            w.Write(limbs.ReadUByte()); // callbackIndex
            limbs.ReadUByte();          // pad to the 0x8 stride
        } else {
            w.Write(limbs.ReadInt16()); // translation.x
            w.Write(limbs.ReadInt16()); // translation.y
            w.Write(limbs.ReadInt16()); // translation.z
        }
    }

    return std::make_shared<RawData>(Finish(w));
}

ExportResult MMKeyFrameSkelBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                  std::string& entryName, YAML::Node& node,
                                                  std::string* replacement) {
    auto data = std::static_pointer_cast<RawData>(raw);
    write.write(data->mBinary.data(), data->mBinary.size());
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> MMKeyFrameAnimFactory::parse(std::vector<uint8_t>& buffer,
                                                                        YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto skelOffset = SegmentedOffset(GetSafeNode<uint32_t>(node, "skel_offset"));

    // The animation is sized by its skeleton: how many limbs, and whether the
    // per-limb bit flags are 8 or 16 bits wide.
    uint8_t limbType = KF_SKEL_NORMAL;
    auto skelNode = Companion::Instance->GetNodeByAddr(skelOffset);
    if (skelNode.has_value()) {
        auto [_, sn] = skelNode.value();
        limbType = ParseLimbType(GetSafeNode<std::string>(sn, "limb_type", "Normal"));
    } else {
        SPDLOG_WARN("MM keyframe anim at 0x{:X}: no skeleton declared at 0x{:X}", offset, skelOffset);
    }
    const uint8_t limbCount = ReadSubArray(buffer, skelOffset, 1).ReadUByte();

    auto head = ReadSubArray(buffer, offset, 0x14);
    const uint32_t bitFlagsAddr = head.ReadUInt32();
    const uint32_t keyFramesAddr = head.ReadUInt32();
    const uint32_t kfNumsAddr = head.ReadUInt32();
    const uint32_t presetValuesAddr = head.ReadUInt32();
    const uint16_t unk10 = head.ReadUInt16();
    const int16_t duration = head.ReadInt16();

    // Each limb's flags say which of its channels are animated: a set bit spends a
    // kfNum, a clear one spends a preset value. Standard skeletons use six bits of
    // a byte, flex ones nine bits of a halfword.
    std::vector<uint16_t> bitFlags;
    uint32_t kfNumsSize = 0, presetValuesSize = 0;
    const uint32_t flagWidth = (limbType == KF_SKEL_FLEX) ? 2 : 1;
    const uint32_t flagMask = (limbType == KF_SKEL_FLEX) ? 0b111111111 : 0b111111;
    const uint32_t flagInvert = (limbType == KF_SKEL_FLEX) ? 0xFFFF : 0xFF;

    auto flags = ReadSubArray(buffer, bitFlagsAddr, limbCount * flagWidth);
    for (uint8_t i = 0; i < limbCount; i++) {
        const uint16_t e = (flagWidth == 2) ? flags.ReadUInt16() : flags.ReadUByte();
        bitFlags.push_back(e);
        kfNumsSize += CountSetBits(e & flagMask);
        presetValuesSize += CountSetBits((e ^ flagInvert) & flagMask);
    }

    std::vector<int16_t> kfNums;
    uint32_t keyFramesCount = 0;
    if (kfNumsSize > 0) {
        auto r = ReadSubArray(buffer, kfNumsAddr, kfNumsSize * 2);
        for (uint32_t i = 0; i < kfNumsSize; i++) {
            const int16_t n = r.ReadInt16();
            keyFramesCount += n;
            kfNums.push_back(n);
        }
    }

    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::MMKeyFrameAnim, 0);
    w.Write(limbType);

    w.Write(static_cast<uint32_t>(bitFlags.size()));
    for (const auto b : bitFlags) {
        if (flagWidth == 2) {
            w.Write(b);
        } else {
            w.Write(static_cast<uint8_t>(b));
        }
    }

    w.Write(keyFramesCount);
    if (keyFramesCount > 0) {
        auto r = ReadSubArray(buffer, keyFramesAddr, keyFramesCount * 6);
        for (uint32_t i = 0; i < keyFramesCount; i++) {
            w.Write(r.ReadInt16()); // frame
            w.Write(r.ReadInt16()); // value
            w.Write(r.ReadInt16()); // velocity
        }
    }

    w.Write(static_cast<uint32_t>(kfNums.size()));
    for (const auto n : kfNums) {
        w.Write(n);
    }

    w.Write(presetValuesSize);
    if (presetValuesSize > 0) {
        auto r = ReadSubArray(buffer, presetValuesAddr, presetValuesSize * 2);
        for (uint32_t i = 0; i < presetValuesSize; i++) {
            w.Write(r.ReadInt16());
        }
    }

    w.Write(unk10);
    w.Write(duration);

    return std::make_shared<RawData>(Finish(w));
}

ExportResult MMKeyFrameAnimBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                  std::string& entryName, YAML::Node& node,
                                                  std::string* replacement) {
    auto data = std::static_pointer_cast<RawData>(raw);
    write.write(data->mBinary.data(), data->mBinary.size());
    return std::nullopt;
}

} // namespace OoT
