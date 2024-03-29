#pragma once

#include "../BaseFactory.h"

namespace SM64 {
class CollisionData : public IParsedData {
public:
    std::vector<int16_t> mCommandList;

    CollisionData(std::vector<int16_t>& commandList) : mCommandList(commandList) {}
};

class CollisionCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class CollisionHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class CollisionBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class CollisionFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code,   CollisionCodeExporter)
            REGISTER(Header, CollisionHeaderExporter)
            REGISTER(Binary, CollisionBinaryExporter)
        };
    }
};
}