#include "OoTArrayFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

static std::shared_ptr<OoTVtxArrayData> parseVtxArray(DataChunk& segment, size_t count) {
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

static std::shared_ptr<OoTVec3sArrayData> parseVec3sArray(DataChunk& segment, size_t count) {
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

std::optional<std::shared_ptr<IParsedData>> OoTArrayFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto count = GetSafeNode<size_t>(node, "count");
    auto arrayType = GetSafeNode<std::string>(node, "array_type");

    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    if (arrayType == "VTX") {
        return parseVtxArray(segment, count);
    }

    if (arrayType == "Vec3s") {
        return parseVec3sArray(segment, count);
    }

    if (arrayType == "Scalar") {
        // ZScalarType, from ZScalar.h: S8 1, U8 2, X8 3, S16 4, U16 5, X16 6,
        // S32 7, U32 8, X32 9.
        const auto scalarType = GetSafeNode<uint32_t>(node, "scalar_type");
        uint32_t width = 1;
        if (scalarType >= 4 && scalarType <= 6) {
            width = 2;
        } else if (scalarType >= 7 && scalarType <= 9) {
            width = 4;
        } else if (scalarType > 9) {
            SPDLOG_ERROR("Unsupported scalar array type {}", scalarType);
            return std::nullopt;
        }

        LUS::BinaryReader reader(segment.data, segment.size);
        reader.SetEndianness(Torch::Endianness::Big);
        std::vector<uint64_t> values;
        for (size_t i = 0; i < count; i++) {
            switch (width) {
                case 2: values.push_back(reader.ReadUInt16()); break;
                case 4: values.push_back(reader.ReadUInt32()); break;
                default: values.push_back(reader.ReadUByte()); break;
            }
        }
        return std::make_shared<OoTScalarArrayData>(scalarType, std::move(values));
    }

    if (arrayType == "CollisionPoly" || arrayType == "Pointer") {
        const auto type = arrayType == "CollisionPoly" ? SohArrayType::CollisionPoly : SohArrayType::Pointer;
        return std::make_shared<OoTUntypedArrayData>(static_cast<uint32_t>(type), count);
    }

    SPDLOG_ERROR("Unknown OoT Array type '{}'", arrayType);
    return std::nullopt;
}

static void exportVtxArray(LUS::BinaryWriter& writer, std::shared_ptr<OoTVtxArrayData> data, bool zeroFlag) {
    writer.Write(static_cast<uint32_t>(SohArrayType::Vertex));
    writer.Write(static_cast<uint32_t>(data->mVtxs.size()));

    for (const auto& v : data->mVtxs) {
        writer.Write(v.ob[0]);
        writer.Write(v.ob[1]);
        writer.Write(v.ob[2]);
        // ZAPD zeroes the flag for display-list-discovered vertices
        // (DisplayListExporter's VTX() text round-trip) but preserves it for
        // XML-declared arrays. zero_flag (set by zapd_to_torch for supplemental
        // VTX arrays) selects the discovered behaviour.
        writer.Write(zeroFlag ? static_cast<uint16_t>(0) : v.flag);
        writer.Write(v.tc[0]);
        writer.Write(v.tc[1]);
        writer.Write(v.cn[0]);
        writer.Write(v.cn[1]);
        writer.Write(v.cn[2]);
        writer.Write(v.cn[3]);
    }
}

static void exportScalarArray(LUS::BinaryWriter& writer, std::shared_ptr<OoTScalarArrayData> data) {
    writer.Write(static_cast<uint32_t>(SohArrayType::Scalar));
    writer.Write(static_cast<uint32_t>(data->mValues.size()));

    // Each element repeats its type tag, then the value at that type's width.
    for (const auto v : data->mValues) {
        writer.Write(data->mScalarType);
        if (data->mScalarType >= 7 && data->mScalarType <= 9) {
            writer.Write(static_cast<uint32_t>(v));
        } else if (data->mScalarType >= 4 && data->mScalarType <= 6) {
            writer.Write(static_cast<uint16_t>(v));
        } else {
            writer.Write(static_cast<uint8_t>(v));
        }
    }
}

static void exportUntypedArray(LUS::BinaryWriter& writer, std::shared_ptr<OoTUntypedArrayData> data) {
    writer.Write(data->mArrayType);
    writer.Write(static_cast<uint32_t>(data->mCount));

    // ArrayExporter has no writer for these element kinds, so each element is a
    // lone type word of NONE with no payload.
    for (size_t i = 0; i < data->mCount; i++) {
        writer.Write(static_cast<uint32_t>(SohScalarType::ZSCALAR_NONE));
    }
}

static void exportVec3sArray(LUS::BinaryWriter& writer, std::shared_ptr<OoTVec3sArrayData> data) {
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

ExportResult OoTArrayBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node,
                                            std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto arrayType = GetSafeNode<std::string>(node, "array_type");

    WriteHeader(writer, Torch::ResourceType::Array, 0);

    if (arrayType == "VTX") {
        bool zeroFlag = node["zero_flag"] && node["zero_flag"].as<bool>();
        exportVtxArray(writer, std::static_pointer_cast<OoTVtxArrayData>(raw), zeroFlag);
    } else if (arrayType == "Vec3s") {
        exportVec3sArray(writer, std::static_pointer_cast<OoTVec3sArrayData>(raw));
    } else if (arrayType == "Scalar") {
        exportScalarArray(writer, std::static_pointer_cast<OoTScalarArrayData>(raw));
    } else if (arrayType == "CollisionPoly" || arrayType == "Pointer") {
        exportUntypedArray(writer, std::static_pointer_cast<OoTUntypedArrayData>(raw));
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT
