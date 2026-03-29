#ifdef OOT_SUPPORT

#include "OoTArrayFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

std::optional<std::shared_ptr<IParsedData>> OoTArrayFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto count = GetSafeNode<size_t>(node, "count");
    auto arrayType = GetSafeNode<std::string>(node, "array_type");

    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    if (arrayType == "VTX") {
        LUS::BinaryReader reader(segment.data, count * sizeof(VtxRaw));
        reader.SetEndianness(Torch::Endianness::Big);
        std::vector<VtxRaw> vertices;

        for (size_t i = 0; i < count; i++) {
            auto x = reader.ReadInt16();
            auto y = reader.ReadInt16();
            auto z = reader.ReadInt16();
            auto flag = reader.ReadUInt16();
            auto tc1 = reader.ReadInt16();
            auto tc2 = reader.ReadInt16();
            auto cn1 = reader.ReadUByte();
            auto cn2 = reader.ReadUByte();
            auto cn3 = reader.ReadUByte();
            auto cn4 = reader.ReadUByte();
            vertices.push_back(VtxRaw({{x, y, z}, flag, {tc1, tc2}, {cn1, cn2, cn3, cn4}}));
        }

        return std::make_shared<OoTVtxArrayData>(vertices);
    }

    if (arrayType == "Vec3s") {
        LUS::BinaryReader reader(segment.data, count * 6);
        reader.SetEndianness(Torch::Endianness::Big);
        std::vector<Vec3s> vecs;

        for (size_t i = 0; i < count; i++) {
            auto x = reader.ReadInt16();
            auto y = reader.ReadInt16();
            auto z = reader.ReadInt16();
            vecs.push_back(Vec3s(x, y, z));
        }

        return std::make_shared<OoTVec3sArrayData>(vecs);
    }

    SPDLOG_ERROR("Unknown OoT Array type '{}'", arrayType);
    return std::nullopt;
}

ExportResult OoTArrayBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node,
                                            std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto arrayType = GetSafeNode<std::string>(node, "array_type");

    WriteHeader(writer, Torch::ResourceType::Array, 0);

    if (arrayType == "VTX") {
        auto data = std::static_pointer_cast<OoTVtxArrayData>(raw);

        writer.Write(static_cast<uint32_t>(SohArrayType::Vertex));
        writer.Write(static_cast<uint32_t>(data->mVtxs.size()));

        for (const auto& v : data->mVtxs) {
            writer.Write(v.ob[0]);
            writer.Write(v.ob[1]);
            writer.Write(v.ob[2]);
            writer.Write(v.flag);
            writer.Write(v.tc[0]);
            writer.Write(v.tc[1]);
            writer.Write(v.cn[0]);
            writer.Write(v.cn[1]);
            writer.Write(v.cn[2]);
            writer.Write(v.cn[3]);
        }
    } else if (arrayType == "Vec3s") {
        auto data = std::static_pointer_cast<OoTVec3sArrayData>(raw);

        writer.Write(static_cast<uint32_t>(SohArrayType::Vector));
        writer.Write(static_cast<uint32_t>(data->mVecs.size()));

        for (const auto& v : data->mVecs) {
            // Per-element: scalar_type (u32) + dimensions (u32) + data
            writer.Write(static_cast<uint32_t>(SohScalarType::ZSCALAR_S16));
            writer.Write(static_cast<uint32_t>(3));
            writer.Write(v.x);
            writer.Write(v.y);
            writer.Write(v.z);
        }
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
