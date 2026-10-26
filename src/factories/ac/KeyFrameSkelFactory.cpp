#include "KeyFrameSkelFactory.h"
#include "ACUtils.h"

namespace AC {

ExportResult KeyFrameSkelBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto skel = std::static_pointer_cast<KeyFrameSkelData>(raw);
    auto writer = LUS::BinaryWriter();
    WriteHeader(writer, Torch::ResourceType::ACKeyFrameSkel, 0);
    writer.Write(skel->mLimbCount);
    writer.Write(skel->mDListCount);
    writer.Write((uint8_t)0);
    writer.Write((uint8_t)skel->mLimbs.size());
    for (const auto& limb : skel->mLimbs) {
        writer.Write(limb.path);
        writer.Write(limb.child);
        writer.Write(limb.flags);
        writer.Write(limb.x);
        writer.Write(limb.y);
        writer.Write(limb.z);
    }
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> KeyFrameSkelFactory::parse(std::vector<uint8_t>& buffer,
                                                                       YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto name = NameOf(node);
    Cursor c(ReadBytes(offset, 8, name), name);
    const uint8_t limbCount = c.U8();
    const uint8_t dListCount = c.U8();
    c.Skip(2);
    const uint32_t table = c.U32();

    std::vector<KeyFrameLimb> limbs;
    Cursor joints(ReadBytes(table, limbCount * 12, name + " joint_table"), name);
    for (uint32_t i = 0; i < limbCount; i++) {
        KeyFrameLimb limb;
        const uint32_t model = joints.U32();
        limb.path = AssetPath(model, "GFX", name + " joint " + std::to_string(i));
        limb.child = joints.U8();
        limb.flags = joints.U8();
        limb.x = joints.S16();
        limb.y = joints.S16();
        limb.z = joints.S16();
        limbs.push_back(limb);
    }
    CheckCount(node, "joints", limbCount, name);
    return std::make_shared<KeyFrameSkelData>(limbCount, dListCount, limbs);
}

} // namespace AC
