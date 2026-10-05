#include "MMCollisionFactory.h"

#include "Companion.h"
#include "utils/Decompressor.h"

namespace MM {

std::optional<std::shared_ptr<IParsedData>> MMCollisionFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto parsed = OoT::OoTCollisionFactory::parse(buffer, node);
    if (!parsed.has_value()) {
        return parsed;
    }

    const auto collision = std::static_pointer_cast<OoT::OoTCollisionData>(parsed.value());
    if (!collision->polygons.empty() || !collision->surfaceTypes.empty()) {
        return parsed;
    }

    auto decodedHeader = Decompressor::AutoDecode(node, buffer, 0x2C);
    LUS::BinaryReader reader(decodedHeader.segment.data, decodedHeader.segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    for (int i = 0; i < 6; ++i) {
        reader.ReadInt16();
    }
    reader.ReadUInt16();
    reader.ReadInt16();
    reader.ReadUInt32();
    reader.ReadUInt16();
    reader.ReadInt16();
    reader.ReadUInt32();
    const uint32_t surfaceTypeAddress = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());
    if (surfaceTypeAddress == 0) {
        if (decodedHeader.root != nullptr && decodedHeader.root->size >= 8) {
            LUS::BinaryReader surfaceTypeReader(decodedHeader.root->data, 8);
            surfaceTypeReader.SetEndianness(Torch::Endianness::Big);
            collision->surfaceTypes.push_back({ surfaceTypeReader.ReadUInt32(), surfaceTypeReader.ReadUInt32() });
        } else if (const auto fileOffset = Companion::Instance->GetFileOffsetFromName();
                   fileOffset.has_value() && fileOffset.value() + 8 <= buffer.size()) {
            LUS::BinaryReader surfaceTypeReader(buffer.data() + fileOffset.value(), 8);
            surfaceTypeReader.SetEndianness(Torch::Endianness::Big);
            collision->surfaceTypes.push_back({ surfaceTypeReader.ReadUInt32(), surfaceTypeReader.ReadUInt32() });
        }
        return parsed;
    }

    YAML::Node surfaceTypeNode;
    surfaceTypeNode["offset"] = surfaceTypeAddress;
    auto [__, surfaceTypeData] = Decompressor::AutoDecode(surfaceTypeNode, buffer, 8);
    LUS::BinaryReader surfaceTypeReader(surfaceTypeData.data, surfaceTypeData.size);
    surfaceTypeReader.SetEndianness(Torch::Endianness::Big);
    collision->surfaceTypes.push_back({ surfaceTypeReader.ReadUInt32(), surfaceTypeReader.ReadUInt32() });
    return parsed;
}

} // namespace MM
