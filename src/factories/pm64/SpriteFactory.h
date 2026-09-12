#pragma once

#include "factories/BaseFactory.h"
#include "types/RawBuffer.h"

// One sprite raster split out of the blob so it can be addressed by name.
struct PM64SpriteRaster {
    uint8_t width;
    uint8_t height;
    std::vector<uint8_t> pixels; // CI4
};

struct PM64SpriteData : public RawBuffer {
    std::vector<PM64SpriteRaster> rasters;
    std::vector<std::vector<uint8_t>> palettes; // 16 x RGBA16, 32 bytes each

    explicit PM64SpriteData(std::vector<uint8_t>& buffer) : RawBuffer(buffer) {}
};

class PM64SpriteBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class PM64SpriteHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class PM64SpriteFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, PM64SpriteHeaderExporter)
            REGISTER(Binary, PM64SpriteBinaryExporter)
        };
    }
};
