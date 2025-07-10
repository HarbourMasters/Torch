#pragma once

#include "factories/BaseFactory.h"
#include <types/RawBuffer.h>
#include <unordered_map>
#include <string>
#include <vector>

namespace BK64 {

typedef struct BKAssetInfo {
    uint32_t offset;
    int16_t compressionFlag;
    int16_t tFlag;
    int32_t assetMode;
    int32_t index;
} BKAssetInfo;

enum class BKAssetType {
    Animation,
    Binary,
    DemoInput,
    Dialog,
    GruntyQuestion,
    LevelSetup,
    Midi,
    Model,
    QuizQuestion,
    Sprite,
};

class BKAssetData : public IParsedData {
public:
    std::vector<BKAssetInfo> mAssetTableInfo;

    BKAssetData(std::vector<BKAssetInfo> assetTableInfo) : mAssetTableInfo(std::move(assetTableInfo)) {}
};

class BKAssetHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BKAssetBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BKAssetCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BKAssetFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, BKAssetHeaderExporter)
            REGISTER(Binary, BKAssetBinaryExporter)
            REGISTER(Code, BKAssetCodeExporter)
        };
    }

    bool HasModdedDependencies() override { return true; }
};

} // namespace BK64
