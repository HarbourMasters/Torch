#include "MMArrayFactory.h"

#include "Companion.h"
#include "factories/oot/OoTArrayFactory.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"

namespace MM {

namespace {

constexpr uint32_t kArrayScalar = 16;
constexpr uint32_t kArrayVector = 24;
constexpr uint32_t kArrayVertex = 25;
constexpr uint32_t kArrayCollisionPoly = 28;
constexpr uint32_t kArrayPointer = 29;
constexpr uint32_t kScalarNone = 0;
constexpr uint32_t kScalarX8 = 3;
char kGfx[] = "Gfx";

std::shared_ptr<MMArrayData> ParseVtx(DataChunk& segment, size_t count) {
    LUS::BinaryReader reader(segment.data, count * sizeof(VtxRaw));
    reader.SetEndianness(Torch::Endianness::Big);
    auto result = std::make_shared<MMArrayData>(MMArrayData::Kind::Vtx, count);
    result->vertices.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto x = reader.ReadInt16();
        const auto y = reader.ReadInt16();
        const auto z = reader.ReadInt16();
        const auto flag = reader.ReadUInt16();
        const auto s = reader.ReadInt16();
        const auto t = reader.ReadInt16();
        const auto r = reader.ReadUByte();
        const auto g = reader.ReadUByte();
        const auto b = reader.ReadUByte();
        const auto a = reader.ReadUByte();
        result->vertices.push_back(VtxRaw({ { x, y, z }, flag, { s, t }, { r, g, b, a } }));
    }
    return result;
}

std::shared_ptr<MMArrayData> ParseVec3s(DataChunk& segment, size_t count) {
    LUS::BinaryReader reader(segment.data, count * 6);
    reader.SetEndianness(Torch::Endianness::Big);
    auto result = std::make_shared<MMArrayData>(MMArrayData::Kind::Vec3s, count);
    result->vectors.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto x = reader.ReadInt16();
        const auto y = reader.ReadInt16();
        const auto z = reader.ReadInt16();
        result->vectors.emplace_back(x, y, z);
    }
    return result;
}

std::shared_ptr<MMArrayData> ParseX8(DataChunk& segment, size_t count) {
    LUS::BinaryReader reader(segment.data, count);
    auto result = std::make_shared<MMArrayData>(MMArrayData::Kind::ScalarX8, count);
    result->bytes.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result->bytes.push_back(reader.ReadUByte());
    }
    return result;
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> MMArrayFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto count = GetSafeNode<size_t>(node, "count");
    const auto arrayType = GetSafeNode<std::string>(node, "array_type");
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    if (arrayType == "VTX") {
        return ParseVtx(segment, count);
    }
    if (arrayType == "Vec3s") {
        return ParseVec3s(segment, count);
    }
    if (arrayType == "GFX") {
        return std::make_shared<MMArrayData>(MMArrayData::Kind::Gfx, count);
    }
    if (arrayType == "Pointer") {
        return std::make_shared<MMArrayData>(MMArrayData::Kind::Pointer, count);
    }
    if (arrayType == "CollisionPoly") {
        return std::make_shared<MMArrayData>(MMArrayData::Kind::CollisionPoly, count);
    }
    if (arrayType == "X8" || (arrayType == "Scalar" && GetSafeNode<uint32_t>(node, "scalar_type") == kScalarX8)) {
        return ParseX8(segment, count);
    }

    SPDLOG_ERROR("Unknown MM Array type '{}'", arrayType);
    return std::nullopt;
}

ExportResult MMArrayBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string&, YAML::Node& node, std::string*) {
    const auto data = std::static_pointer_cast<MMArrayData>(raw);
    LUS::BinaryWriter writer;
    WriteHeader(writer, Torch::ResourceType::Array, 0);

    switch (data->kind) {
        case MMArrayData::Kind::Vtx: {
            writer.Write(kArrayVertex);
            writer.Write(static_cast<uint32_t>(data->vertices.size()));
            const bool zeroFlag = node["zero_flag"] && node["zero_flag"].as<bool>();
            const bool sunTc = node["sun_tc"] && node["sun_tc"].as<bool>();
            for (const auto& v : data->vertices) {
                writer.Write(v.ob[0]);
                writer.Write(v.ob[1]);
                writer.Write(v.ob[2]);
                writer.Write(zeroFlag ? static_cast<uint16_t>(0) : v.flag);
                writer.Write(v.tc[0]);
                const int16_t t = sunTc ? static_cast<int16_t>((((v.tc[1] >> 5) - 1) / 2) << 5) : v.tc[1];
                writer.Write(t);
                writer.Write(v.cn[0]);
                writer.Write(v.cn[1]);
                writer.Write(v.cn[2]);
                writer.Write(v.cn[3]);
            }
            break;
        }
        case MMArrayData::Kind::Vec3s:
            writer.Write(kArrayVector);
            writer.Write(static_cast<uint32_t>(data->vectors.size()));
            for (const auto& v : data->vectors) {
                writer.Write(static_cast<uint32_t>(OoT::SohScalarType::ZSCALAR_S16));
                writer.Write(static_cast<uint32_t>(3));
                writer.Write(v.x);
                writer.Write(v.y);
                writer.Write(v.z);
            }
            break;
        case MMArrayData::Kind::ScalarX8:
            writer.Write(kArrayScalar);
            writer.Write(static_cast<uint32_t>(data->bytes.size()));
            for (const auto value : data->bytes) {
                writer.Write(kScalarX8);
                writer.Write(value);
            }
            break;
        case MMArrayData::Kind::Gfx:
            writer.Write(kArrayPointer);
            writer.Write(static_cast<uint32_t>(data->count));
            for (size_t i = 0; i < data->count; ++i) {
                writer.Write(kGfx, sizeof(kGfx));
            }
            break;
        case MMArrayData::Kind::Pointer:
            writer.Write(kArrayPointer);
            writer.Write(static_cast<uint32_t>(data->count));
            for (size_t i = 0; i < data->count; ++i) {
                writer.Write(kGfx, sizeof(kGfx));
            }
            break;
        case MMArrayData::Kind::CollisionPoly:
            writer.Write(kArrayCollisionPoly);
            writer.Write(static_cast<uint32_t>(data->count));
            for (size_t i = 0; i < data->count; ++i) {
                writer.Write(kScalarNone);
            }
            break;
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace MM
