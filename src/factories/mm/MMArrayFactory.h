#pragma once

#include "factories/BaseFactory.h"
#include "factories/VtxFactory.h"
#include <types/Vec3D.h>

#include <vector>

namespace MM {

class MMArrayData : public IParsedData {
public:
    enum class Kind {
        Vtx,
        Vec3s,
        ScalarX8,
        Gfx,
        Pointer,
        CollisionPoly,
    };

    Kind kind;
    size_t count;
    std::vector<VtxRaw> vertices;
    std::vector<Vec3s> vectors;
    std::vector<uint8_t> bytes;

    MMArrayData(Kind kind, size_t count) : kind(kind), count(count) {}
};

class MMArrayBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MMArrayFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, MMArrayBinaryExporter) };
    }
    uint32_t GetAlignment() override { return 8; };
};

} // namespace MM
