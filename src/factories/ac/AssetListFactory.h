#pragma once

#include "factories/BaseFactory.h"

#include <string>
#include <vector>

namespace AC {

class AssetListData : public IParsedData {
public:
    std::vector<std::string> mPaths;

    explicit AssetListData(std::vector<std::string> paths) : mPaths(std::move(paths)) {}
};

class AssetListBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AssetListFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, AssetListBinaryExporter)
        };
    }
};

} // namespace AC
