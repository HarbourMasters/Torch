#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include <vector>
#include <string>
#include <optional>

namespace OoT {

enum class OoTAnimationType : uint32_t {
    Normal = 0,
    Link = 1,
    Curve = 2,
    Legacy = 3,
};

struct RotationIndex {
    uint16_t x, y, z;
};

struct CurveInterpKnot {
    uint16_t unk_00;
    int16_t unk_02;
    int16_t unk_04;
    int16_t unk_06;
    float unk_08;
};

class OoTNormalAnimationData : public IParsedData {
public:
    int16_t frameCount;
    std::vector<uint16_t> rotationValues;
    std::vector<RotationIndex> rotationIndices;
    int16_t limit;
    bool isLegacy = false;
};

class OoTCurveAnimationData : public IParsedData {
public:
    int16_t frameCount;
    std::vector<uint8_t> refIndexArr;
    std::vector<CurveInterpKnot> transformDataArr;
    std::vector<int16_t> copyValuesArr;
};

class OoTAnimationBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTAnimationFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTAnimationBinaryExporter)
        };
    }
};

class OoTCurveAnimationBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTCurveAnimationFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTCurveAnimationBinaryExporter)
        };
    }
};

class OoTPlayerAnimationHeaderData : public IParsedData {
public:
    int16_t frameCount;
    std::string animDataPath;
};

class OoTPlayerAnimationHeaderBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTPlayerAnimationHeaderFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTPlayerAnimationHeaderBinaryExporter)
        };
    }
};

class OoTPlayerAnimationData : public IParsedData {
public:
    std::vector<int16_t> limbRotData;
};

class OoTPlayerAnimationDataBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTPlayerAnimationDataFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTPlayerAnimationDataBinaryExporter)
        };
    }
};

} // namespace OoT

#endif
