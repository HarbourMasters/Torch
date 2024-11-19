#pragma once

#include <factories/BaseFactory.h>
#include "AudioContext.h"

class NSampleData : public IParsedData {
public:
    uint32_t codec;
    uint32_t medium;
    uint32_t unk;
    uint32_t isRelocated;
    uint32_t size;
    uint32_t sampleAddr;
    uint32_t loop;
    uint32_t book;
    uint32_t sampleBankId;
};

class NSampleHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class NSampleBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class NSampleCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class NSampleFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, NSampleHeaderExporter)
            REGISTER(Binary, NSampleBinaryExporter)
            REGISTER(Code, NSampleCodeExporter)
        };
    }
};
