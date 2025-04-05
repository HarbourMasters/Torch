#pragma once

#include <factories/BaseFactory.h>
#include <types/RawBuffer.h>
#include <unordered_map>
#include <string>

namespace BK64 {

class BinaryAssetData : public IParsedData {
public:
    std::vector<uint8_t> mBuffer;
    std::string mSubtype;
    bool mCompressed;
    int mTFlag;
    int mIndex;
    
    BinaryAssetData(std::vector<uint8_t> buffer, const std::string& subtype, int tFlag, int index) 
        : mBuffer(std::move(buffer)), mSubtype(subtype), mTFlag(tFlag), mIndex(index) {}
};

class BinaryAssetHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BinaryAssetBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BinaryAssetCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BinaryAssetFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, BinaryAssetHeaderExporter)
            REGISTER(Binary, BinaryAssetBinaryExporter)
            REGISTER(Code, BinaryAssetCodeExporter)
        };
    }
    bool SupportModdedAssets() override { return true; }
};

} // namespace BK64