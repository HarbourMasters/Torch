#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <vector>
#include <string>
#include <optional>

namespace OoT {

struct CollisionVertex {
    int16_t x, y, z;
};

struct CollisionPoly {
    uint16_t type;
    uint16_t vtxA;
    uint16_t vtxB;
    uint16_t vtxC;
    uint16_t normX;
    uint16_t normY;
    uint16_t normZ;
    uint16_t dist;
};

struct SurfaceType {
    uint32_t data0;
    uint32_t data1;
};

struct CamDataEntry {
    uint16_t cameraSType;
    int16_t numData;
    uint32_t cameraPosIndex;
};

struct CamPosData {
    int16_t x, y, z;
};

struct WaterBox {
    int16_t xMin;
    int16_t ySurface;
    int16_t zMin;
    int16_t xLength;
    int16_t zLength;
    uint32_t properties;
};

class OoTCollisionData : public IParsedData {
public:
    int16_t absMinX, absMinY, absMinZ;
    int16_t absMaxX, absMaxY, absMaxZ;
    std::vector<CollisionVertex> vertices;
    std::vector<CollisionPoly> polygons;
    std::vector<SurfaceType> surfaceTypes;
    std::vector<CamDataEntry> camDataEntries;
    std::vector<CamPosData> camPositions;
    std::vector<WaterBox> waterBoxes;
};

class OoTCollisionBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTCollisionFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTCollisionBinaryExporter)
        };
    }
};

} // namespace OoT

#endif
