#pragma once

#include "factories/BaseFactory.h"
#include <string>
#include <types/RawBuffer.h>
#include <unordered_map>
#include <vector>

namespace BK64 {

/**
 * One asset-table entry: where an asset lives and what it is.
 *
 * Fields:
 * - offset: ROM address or file offset (BKZIP compressed if compressionFlag != 0)
 * - compressionFlag: 0 = uncompressed, 1 = BKZIP compressed, 2 = MIO0, 3 = YAY0
 * - tFlag: Type discriminator (0=model, 1=sprite, 2=animation, etc.)
 * - assetMode: Loading mode (0=immediate, 1=deferred, 2=cached)
 * - index: Original asset index in source data (debugging aid)
 *
 * How a lookup flows:
 *   getModel3d(0x2d5) → assetTable[0x2d5] → offset=0x1A2400, compression=1
 *   -> Decompressor::BKZIP(ROM + 0x1A2400) → ModelFactory::parse()
 */
typedef struct BKAssetInfo {
    uint32_t offset;
    int16_t compressionFlag;
    int16_t tFlag;
    int32_t assetMode;
    int32_t index;
} BKAssetInfo;

enum class BKAssetType {
    Animation,      // 0x000 - 0x0FF
    Binary,         // 0x100 - 0x1FF
    DemoInput,      // 0x200 - 0x2CF
    Dialog,         // 0x2D0 - 0x2D0
    Model,          // 0x2D1 - 0x571
    Sprite,         // 0x572 - 0x6FF
    Map,            // 0x700 - 0x7FF
    Midi,           // 0x800 - 0x8FF
    GruntyQuestion, // 0x900 - 0x9FF
    QuizQuestion,   // 0xA00 - 0xAFF
};

class BKAssetData : public IParsedData {
  public:
    std::vector<BKAssetInfo> mAssetTableInfo;
    std::unordered_map<uint32_t, std::string> mSymbolMap;

    BKAssetData(std::vector<BKAssetInfo> assetTableInfo, std::unordered_map<uint32_t, std::string> symbolMap)
        : mAssetTableInfo(std::move(assetTableInfo)), mSymbolMap(std::move(symbolMap)) {
    }
};

class BKAssetHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class BKAssetBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class BKAssetCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class BKAssetFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Header, BKAssetHeaderExporter) REGISTER(Binary, BKAssetBinaryExporter)
                     REGISTER(Code, BKAssetCodeExporter) };
    }

    bool HasModdedDependencies() override {
        return true;
    }

    bool IsDialogPackRoot() const override {
        return true;
    }

    void PreprocessConfig(YAML::Node& cfg, N64::Cartridge* cart) override;
};

} // namespace BK64
