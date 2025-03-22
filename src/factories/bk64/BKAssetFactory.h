#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

class AssetData : public IParsedData {
  public:

    int32_t mOffset; //(usv1.0 assets.bin rom offset = 0x5E90) + asset offset = address of asset
    std::vector<uint8_t> mRawBinary;
    bool mIsCompressed = false;
    int8_t mAssetFlag;
    size_t mLength;

    AssetData() = default;

    AssetData(int32_t offset, std::vector<uint8_t> data, int8_t assetFlag) {
        mOffset = offset;
        //mLength = size;
        mRawBinary = data;
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
