#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include "factories/VtxFactory.h"
#include <types/Vec3D.h>
#include <variant>
#include <vector>

namespace OoT {

// Shipwright's ArrayResourceType enum values (must match reference O2R format)
enum class SohArrayType : uint32_t {
    Vector = 24,
    Vertex = 25,
};

// Shipwright's ZScalarType enum values (from ZAPDTR/ZAPD/ZScalar.h)
enum class SohScalarType : uint32_t {
    ZSCALAR_NONE = 0,
    ZSCALAR_S8 = 1,
    ZSCALAR_U8 = 2,
    ZSCALAR_X8 = 3,
    ZSCALAR_S16 = 4,
    ZSCALAR_U16 = 5,
};

// Parsed data for OoT Array (Vertex variant)
class OoTVtxArrayData : public IParsedData {
public:
    std::vector<VtxRaw> mVtxs;
    explicit OoTVtxArrayData(std::vector<VtxRaw> vtxs) : mVtxs(std::move(vtxs)) {}
};

// Parsed data for OoT Array (Vec3s variant)
class OoTVec3sArrayData : public IParsedData {
public:
    std::vector<Vec3s> mVecs;
    explicit OoTVec3sArrayData(std::vector<Vec3s> vecs) : mVecs(std::move(vecs)) {}
};

class OoTArrayBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTArrayFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTArrayBinaryExporter)
        };
    }
    uint32_t GetAlignment() override { return 8; };
};

} // namespace OoT

#endif
