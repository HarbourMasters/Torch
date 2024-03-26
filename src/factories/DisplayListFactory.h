#pragma once

#include "BaseFactory.h"
#include "n64/CommandMacros.h"
#include <tuple>

class DListData : public IParsedData {
public:
    std::vector<uint32_t> mGfxs;

    DListData() {}
    DListData(std::vector<uint32_t> gfxs) : mGfxs(gfxs) {}

    std::optional<std::vector<uint8_t>> ToCacheBuffer() override {
        std::vector<uint8_t> buffer;
        buffer.insert(buffer.end(), reinterpret_cast<uint8_t *>(mGfxs.data()), reinterpret_cast<uint8_t *>(mGfxs.data()) + mGfxs.size() * sizeof(uint32_t));
        return buffer;
    }

    void FromCacheBuffer(std::vector<uint8_t>& buffer) override {
        mGfxs = std::vector(reinterpret_cast<uint32_t *>(buffer.data()), reinterpret_cast<uint32_t *>(buffer.data()) + buffer.size() / sizeof(uint32_t));
    }
};

class DListHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class DListBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class DListCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class DListFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, DListHeaderExporter)
            REGISTER(Binary, DListBinaryExporter)
            REGISTER(Code, DListCodeExporter)
        };
    }
    uint32_t GetAlignment() override {
        return 8;
    }
    bool IsCacheable() override {
        return true;
    }
    virtual std::optional<std::shared_ptr<IParsedData>> CreateDataPointer() {
        return std::make_shared<DListData>();
    }
};
