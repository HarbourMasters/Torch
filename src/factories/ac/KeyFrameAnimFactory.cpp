#include "KeyFrameAnimFactory.h"
#include "ACUtils.h"

#include <bit>

namespace AC {

KeyFrameCounts CountKeyFrameTables(const std::vector<uint8_t>& flags) {
    uint32_t keys = 0;
    if (!flags.empty()) {
        keys += std::popcount((uint32_t)(flags[0] & 0x38));
    }
    for (const auto f : flags) {
        keys += std::popcount((uint32_t)(f & 0x07));
    }
    const uint32_t total = 3 + 3 * (uint32_t)flags.size();
    return { keys, total - keys };
}

KeyFrameAnimData ReadKeyFrameAnim(uint32_t addr, YAML::Node& node, const std::string& name) {
    const auto joints = GetSafeNode<uint32_t>(node, "joints");
    if (joints == 0 || joints > 255) {
        throw std::runtime_error(name + ": joints must be 1..255");
    }
    Cursor c(ReadBytes(addr, 0x14, name), name);
    const uint32_t flagPtr = c.U32();
    const uint32_t dataPtr = c.U32();
    const uint32_t keyPtr = c.U32();
    const uint32_t fixedPtr = c.U32();

    KeyFrameAnimData anim;
    anim.mPad = c.S16();
    anim.mFrames = c.S16();
    anim.mFlags = ReadBytes(flagPtr, joints, name + " flag_table");

    const auto counts = CountKeyFrameTables(anim.mFlags);
    Cursor keys(ReadBytes(keyPtr, counts.keys * 2, name + " key_table"), name);
    uint32_t total = 0;
    for (uint32_t i = 0; i < counts.keys; i++) {
        const int16_t n = keys.S16();
        if (n < 0) {
            throw std::runtime_error(name + ": negative key count in key_table");
        }
        anim.mKeyCounts.push_back(n);
        total += n;
    }
    Cursor data(ReadBytes(dataPtr, total * 6, name + " data_table"), name);
    for (uint32_t i = 0; i < total; i++) {
        KeyFrame k;
        k.frame = data.S16();
        k.value = data.S16();
        k.velocity = data.S16();
        anim.mKeyFrames.push_back(k);
    }
    Cursor fixed(ReadBytes(fixedPtr, counts.fixed * 2, name + " fixed_table"), name);
    for (uint32_t i = 0; i < counts.fixed; i++) {
        anim.mFixedValues.push_back(fixed.S16());
    }

    CheckCount(node, "flags", joints, name);
    CheckCount(node, "keys", counts.keys, name);
    CheckCount(node, "fixed", counts.fixed, name);
    CheckCount(node, "data", total * 3, name);
    return anim;
}

void WriteKeyFrameAnimBody(LUS::BinaryWriter& writer, const KeyFrameAnimData& anim) {
    writer.Write((uint8_t)0);
    writer.Write((uint32_t)anim.mFlags.size());
    for (const auto f : anim.mFlags) {
        writer.Write(f);
    }
    writer.Write((uint32_t)anim.mKeyFrames.size());
    for (const auto& k : anim.mKeyFrames) {
        writer.Write(k.frame);
        writer.Write(k.value);
        writer.Write(k.velocity);
    }
    writer.Write((uint32_t)anim.mKeyCounts.size());
    for (const auto k : anim.mKeyCounts) {
        writer.Write(k);
    }
    writer.Write((uint32_t)anim.mFixedValues.size());
    for (const auto v : anim.mFixedValues) {
        writer.Write(v);
    }
    writer.Write((uint16_t)anim.mPad);
    writer.Write(anim.mFrames);
}

ExportResult KeyFrameAnimBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto anim = std::static_pointer_cast<KeyFrameAnimData>(raw);
    auto writer = LUS::BinaryWriter();
    WriteHeader(writer, Torch::ResourceType::ACKeyFrameAnim, 0);
    WriteKeyFrameAnimBody(writer, *anim);
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> KeyFrameAnimFactory::parse(std::vector<uint8_t>& buffer,
                                                                       YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    return std::make_shared<KeyFrameAnimData>(ReadKeyFrameAnim(offset, node, NameOf(node)));
}

} // namespace AC
