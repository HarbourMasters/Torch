#include "GenericArrayFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#include <cstdlib>

#define FORMAT_INT(x, w) std::dec << std::setfill(' ') << std::setw(w) << x
#define GET_MAG(num) ((uint32_t)((abs(int(num)) > 1) ? std::log10(abs(int(num))) : 1))
#define GET_MAG_U(num) ((uint32_t)((num > 1) ? std::log10(num) : 1))

std::unordered_map<std::string, ArrayType> arrayTypeMap = {
    { "u8", ArrayType::u8 },
    { "s8", ArrayType::s8 },
    { "u16", ArrayType::u16 },
    { "s16", ArrayType::s16 },
    { "u32", ArrayType::u32 },
    { "s32", ArrayType::s32 },
    { "u64", ArrayType::u64 },
    { "f32", ArrayType::f32 },
    { "f64", ArrayType::f64 },
    { "Vec2f", ArrayType::Vec2f },
    { "Vec3f", ArrayType::Vec3f },
    { "Vec3s", ArrayType::Vec3s },
    { "Vec3i", ArrayType::Vec3i },
    { "Vec4f", ArrayType::Vec4f },
    { "Vec4s", ArrayType::Vec4s },
};

std::unordered_map<ArrayType, size_t> typeSizeMap = {
    { ArrayType::u8, 1 },
    { ArrayType::s8, 1 },
    { ArrayType::u16, 2 },
    { ArrayType::s16, 2 },
    { ArrayType::u32, 4 },
    { ArrayType::s32, 4 },
    { ArrayType::u64, 8 },
    { ArrayType::f32, 4 },
    { ArrayType::f64, 8 },
    { ArrayType::Vec2f, 8 },
    { ArrayType::Vec3f, 12 },
    { ArrayType::Vec3s, 6 },
    { ArrayType::Vec3i, 12 },
    { ArrayType::Vec4f, 16 },
    { ArrayType::Vec4s, 8 },
};

static int GetPrecision(float f) {
    int p = 0;
    int shift = 1;
    float approx = std::round(f);

    while(f != approx && p < 12 ){
        shift *= 10;
        p++;
        approx = std::round(f * shift) / shift;
    }
    return p;
}

GenericArray::GenericArray(std::vector<ArrayDatum> data) : mData(std::move(data)) {
    mMaxWidth = 1;
    for (auto &datum : data) {
        switch (static_cast<ArrayType>(datum.index())) {
            case ArrayType::u8: {
                mMaxWidth = std::max(mMaxWidth, GET_MAG_U(std::get<uint32_t>(datum)));
                break;
            }
            case ArrayType::s8: {
                mMaxWidth = std::max(mMaxWidth, GET_MAG(std::get<int32_t>(datum)));
                break;
            }
            case ArrayType::u16: {
                mMaxWidth = std::max(mMaxWidth, GET_MAG_U(std::get<uint16_t>(datum)));
                break;
            }
            case ArrayType::s16: {
                mMaxWidth = std::max(mMaxWidth, GET_MAG(std::get<int16_t>(datum)));
                break;
            }
            case ArrayType::u32: {
                mMaxWidth = std::max(mMaxWidth, GET_MAG_U(std::get<uint32_t>(datum)));
                break;
            }
            case ArrayType::s32: {
                mMaxWidth = std::max(mMaxWidth, GET_MAG(std::get<int32_t>(datum)));
                break;
            }
            case ArrayType::u64: {
                mMaxWidth = std::max(mMaxWidth, GET_MAG_U(std::get<uint64_t>(datum)));
                break;
            }
            case ArrayType::f32: {
                auto floatNum = std::get<float>(datum);
                mMaxWidth = std::max(mMaxWidth, GET_MAG(floatNum) + 1 + GetPrecision(floatNum));
                break;
            }
            case ArrayType::f64: {
                auto doubleNum = std::get<double>(datum);
                mMaxWidth = std::max(mMaxWidth, GET_MAG(doubleNum) + 1 + GetPrecision(doubleNum));
                break;
            }
            case ArrayType::Vec2f: {
                mMaxWidth = std::max(mMaxWidth, (uint32_t)std::get<Vec2f>(datum).width());
                break;
            }
            case ArrayType::Vec3f: {
                mMaxWidth = std::max(mMaxWidth, (uint32_t)std::get<Vec3f>(datum).width());
                break;
            }
            case ArrayType::Vec3s: {
                mMaxWidth = std::max(mMaxWidth, (uint32_t)std::get<Vec3s>(datum).width());
                break;
            }
            case ArrayType::Vec3i: {
                mMaxWidth = std::max(mMaxWidth, (uint32_t)std::get<Vec3i>(datum).width());
                break;
            }
            case ArrayType::Vec4f: {
                mMaxWidth = std::max(mMaxWidth,(uint32_t)std::get<Vec4f>(datum).width());
                break;
            }
            case ArrayType::Vec4s: {
                mMaxWidth = std::max(mMaxWidth, (uint32_t)std::get<Vec4s>(datum).width());
                break;
            }
        }
    }
}

ExportResult ArrayHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto type = GetSafeNode<std::string>(node, "array_type");

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern " << type << " " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult ArrayCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto type = GetSafeNode<std::string>(node, "array_type");
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto array = std::static_pointer_cast<GenericArray>(raw);

    if (arrayTypeMap.find(type) == arrayTypeMap.end()) {
        SPDLOG_ERROR("Unknown Generic Array type '{}'", type);
        throw std::runtime_error("Unknown Generic Array type " + type);
    }

    ArrayType arrayType = arrayTypeMap.at(type);

    size_t typeSize = typeSizeMap.at(arrayType);


    write << type << " " << symbol << "[] = {";

    int columnCount = 120 / (array->mMaxWidth + 8);
    int i = 0;
    for (auto &datum : array->mData) {
        if ((i++ % columnCount) == 0) {
            write << "\n" << fourSpaceTab;
        }
        switch (static_cast<ArrayType>(datum.index())) {
            case ArrayType::u8:
                write << FORMAT_INT(std::get<uint8_t>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::s8:
                write << FORMAT_INT(std::get<int8_t>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::u16:
                write << FORMAT_INT(std::get<uint16_t>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::s16:
                write << FORMAT_INT(std::get<int16_t>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::u32:
                write << FORMAT_INT(std::get<uint32_t>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::s32:
                write << FORMAT_INT(std::get<int32_t>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::u64:
                write << FORMAT_INT(std::get<uint64_t>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::f32:
                write << FORMAT_INT(std::get<float>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::f64:
                write << FORMAT_INT(std::get<double>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::Vec2f:
                write << FORMAT_INT(std::get<Vec2f>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::Vec3f:
                write << FORMAT_INT(std::get<Vec3f>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::Vec3s:
                write << FORMAT_INT(std::get<Vec3s>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::Vec3i:
                write << FORMAT_INT(std::get<Vec3i>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::Vec4f:
                write << FORMAT_INT(std::get<Vec4f>(datum), array->mMaxWidth) << ", ";
                break;
            case ArrayType::Vec4s:
                write << FORMAT_INT(std::get<Vec4s>(datum), array->mMaxWidth) << ", ";
                break;
        }
    }

    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// Count: " << array->mData.size() << " " << type << "\n";
    }

    return offset + array->mData.size() * typeSize;
}

ExportResult ArrayBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> GenericArrayFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    std::vector<ArrayDatum> data;
    const auto count = GetSafeNode<uint32_t>(node, "count");
    const auto type = GetSafeNode<std::string>(node, "array_type");

    if (arrayTypeMap.find(type) == arrayTypeMap.end()) {
        SPDLOG_ERROR("Unknown Generic Array type '{}'", type);
        throw std::runtime_error("Unknown Generic Array type " + type);
    }

    ArrayType arrayType = arrayTypeMap.at(type);

    size_t typeSize = typeSizeMap.at(arrayType);

    auto [root, segment] = Decompressor::AutoDecode(node, buffer, count * typeSize);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    for (int i = 0; i < count; i++) {
        switch (arrayType) {
            case ArrayType::u8: {
                auto x = reader.ReadUByte();
                data.emplace_back(x);
                break;
            }
            case ArrayType::s8: {
                auto x = reader.ReadInt8();
                data.emplace_back(x);
                break;
            }
            case ArrayType::u16: {
                auto x = reader.ReadUInt16();
                data.emplace_back(x);
                break;
            }
            case ArrayType::s16: {
                auto x = reader.ReadInt16();
                data.emplace_back(x);
                break;
            }
            case ArrayType::u32: {
                auto x = reader.ReadUInt32();
                data.emplace_back(x);
                break;
            }
            case ArrayType::s32: {
                auto x = reader.ReadInt32();
                data.emplace_back(x);
                break;
            }
            case ArrayType::u64: {
                auto x = reader.ReadUInt64();
                data.emplace_back(x);
                break;
            }
            case ArrayType::f32: {
                auto x = reader.ReadFloat();
                data.emplace_back(x);
                break;
            }
            case ArrayType::f64: {
                auto x = reader.ReadDouble();
                data.emplace_back(x);
                break;
            }
            case ArrayType::Vec2f: {
                auto vx = reader.ReadFloat();
                auto vy = reader.ReadFloat();

                data.emplace_back(Vec2f(vx, vy));
                break;
            }
            case ArrayType::Vec3f: {
                auto vx = reader.ReadFloat();
                auto vy = reader.ReadFloat();
                auto vz = reader.ReadFloat();

                data.emplace_back(Vec3f(vx, vy, vz));
                break;
            }
            case ArrayType::Vec3s: {
                auto vx = reader.ReadInt16();
                auto vy = reader.ReadInt16();
                auto vz = reader.ReadInt16();

                data.emplace_back(Vec3s(vx, vy, vz));
                break;
            }
            case ArrayType::Vec3i: {
                auto vx = reader.ReadInt32();
                auto vy = reader.ReadInt32();
                auto vz = reader.ReadInt32();

                data.emplace_back(Vec3i(vx, vy, vz));
                break;
            }
            case ArrayType::Vec4f: {
                auto vx = reader.ReadFloat();
                auto vy = reader.ReadFloat();
                auto vz = reader.ReadFloat();
                auto vw = reader.ReadFloat();

                data.emplace_back(Vec4f(vx, vy, vz, vw));
                break;
            }
            case ArrayType::Vec4s: {
                auto vx = reader.ReadInt16();
                auto vy = reader.ReadInt16();
                auto vz = reader.ReadInt16();
                auto vw = reader.ReadInt16();

                data.emplace_back(Vec4s(vx, vy, vz, vw));
                break;
            }
        }
    }

    return std::make_shared<GenericArray>(data);
}
