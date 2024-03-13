#pragma once

#include "../BaseFactory.h"
#include "types/Vec3D.h"

namespace SF64 {

struct CollisionPoly {
    int16_t tri[3];
    int16_t unk_06;
    Vec3s norm;
    int16_t unk_0E;
    int32_t dist;
};

class ColPolyData : public IParsedData {
public:
    std::vector<CollisionPoly> mPolys;
    std::vector<Vec3s> mMesh;

    ColPolyData(std::vector<CollisionPoly> polys, std::vector<Vec3s> mesh);
};

class ColPolyHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class ColPolyBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class ColPolyCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class ColPolyFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, ColPolyCodeExporter)
            REGISTER(Header, ColPolyHeaderExporter)
            REGISTER(Binary, ColPolyBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};
}
