#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

class AssetData : public IParsedData {
  public:

    enum class AssetType {
        Animation,
        Binary,
        DemoInput,
        Dialog,
        GruntyQuestion,
        LevelSetup,
        Midi,
        Model,
        QuizQuestion,
        Sprite  // Special case, needs extra handling in OG asset tool and starship, may not be needed here?
    };

    AssetType mType;
    int32_t mOffset; //(usv1.0 assets.bin rom offset = 0x5E90) + asset offset = address of asset
    std::vector<uint8_t> mRawBinary;
    bool mIsCompressed = false;
    int8_t mAssetFlag;
    int mSize;

    AssetData() = default;

    AssetData(int32_t offset, int size, std::vector<uint8_t> data, AssetType type, bool compressed, int8_t assetFlag) {
        mOffset = offset;
        mSize = size;
        mRawBinary = data;
        mType = type;
        mIsCompressed = compressed;
        mAssetFlag = assetFlag;
    }
};

class AssetHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AssetBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AssetCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AssetFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { 
            REGISTER(Code, AssetCodeExporter) 
            REGISTER(Header, AssetHeaderExporter)                     
            REGISTER(Binary, AssetBinaryExporter) 
        };
    }
};
} // namespace BK64
