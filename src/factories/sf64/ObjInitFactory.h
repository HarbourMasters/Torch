#pragma once

#include "../BaseFactory.h"
#include "types/Vec3D.h"

namespace SF64 {

struct ObjectInit {
    /* 0x00 */ float zPos1;
    /* 0x04 */ int16_t zPos2;
    /* 0x06 */ int16_t xPos;
    /* 0x08 */ int16_t yPos;
    /* 0x0A */ Vec3s rot;
    /* 0x10 */ int16_t id;
};

class ObjInitData : public IParsedData {
public:
    std::vector<ObjectInit> mObjInit;

    explicit ObjInitData(std::vector<ObjectInit> objInit) : mObjInit(objInit) {}
};

class ObjInitHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class ObjInitBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class ObjInitCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class ObjInitFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, ObjInitHeaderExporter)
            REGISTER(Binary, ObjInitBinaryExporter)
            REGISTER(Code, ObjInitCodeExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};
}