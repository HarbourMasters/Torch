#pragma once

#include "BaseFactory.h"
#include <vector>
#include <variant>
#include <types/Vec3D.h>

typedef std::variant<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, float, double, Vec2f, Vec3f, Vec3s, Vec3i, Vec4f, Vec4s> ArrayDatum;

enum class ArrayType {
    u8, s8, u16, s16, u32, s32, u64, f32, f64, Vec2f, Vec3f, Vec3s, Vec3i, Vec4f, Vec4s,
};

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

class GenericArray : public IParsedData {
public:
    uint32_t mMaxWidth;
    std::vector<ArrayDatum> mData;

    explicit GenericArray(std::vector<ArrayDatum> data);
};

class ArrayCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class ArrayHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class ArrayBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class GenericArrayFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, ArrayHeaderExporter)
            REGISTER(Binary, ArrayBinaryExporter)
            REGISTER(Code, ArrayCodeExporter)
        };
    }
};
