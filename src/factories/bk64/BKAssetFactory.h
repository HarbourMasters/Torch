#pragma once

#include "BaseFactory.h"
#include "../types/RawBuffer.h"
#include <unordered_map>
#include <string>

class BinaryAssetData : public IParsedData {
public:
    std::vector<uint8_t> mBuffer;
    std::string mSubtype;
    bool mCompressed;
    int mTFlag;
    
    BinaryAssetData(std::vector<uint8_t> buffer, const std::string& subtype, bool compressed, int tFlag) 
        : mBuffer(std::move(buffer)), mSubtype(subtype), mCompressed(compressed), mTFlag(tFlag) {}
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

class BinaryAssetXMLExporter : public BaseExporter {
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
            REGISTER(XML, BinaryAssetXMLExporter)
        };
    }
    bool SupportModdedAssets() override { return true; }
};