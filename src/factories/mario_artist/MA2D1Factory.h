#pragma once

#include <factories/BaseFactory.h>

namespace MA {

class MA2D1Data : public IParsedData {
public:
    std::string mFormat;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mSize;

    MA2D1Data(std::string format, uint32_t width, uint32_t height, uint32_t size) : 
        mFormat(format),
        mWidth(width),
        mHeight(height),
        mSize(size) {}
};

class MA2D1HeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MA2D1BinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MA2D1CodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MA2D1Factory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, MA2D1CodeExporter)
            REGISTER(Header, MA2D1HeaderExporter)
            REGISTER(Binary, MA2D1BinaryExporter)
        };
    }
    bool HasModdedDependencies() override { return true; }
};
}
