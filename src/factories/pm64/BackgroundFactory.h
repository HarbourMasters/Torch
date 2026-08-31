#pragma once

#include "factories/BaseFactory.h"
#include "utils/TextureUtils.h"

// Parsed background data: raster (CI8) + palette(s) + metadata
struct PM64BackgroundData : public IParsedData {
    uint16_t width;
    uint16_t height;
    uint16_t startX;
    uint16_t startY;
    uint32_t palCount;
    std::vector<uint8_t> raster;                // CI8 pixel indices
    std::vector<std::vector<uint8_t>> palettes; // Each palette is 512 bytes (256 × RGBA16)

    PM64BackgroundData(uint16_t w, uint16_t h, uint16_t sx, uint16_t sy, std::vector<uint8_t> raster,
                       std::vector<std::vector<uint8_t>> palettes)
        : width(w), height(h), startX(sx), startY(sy), palCount(palettes.size()), raster(std::move(raster)),
          palettes(std::move(palettes)) {
    }
};

class PM64BackgroundBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class PM64BackgroundHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class PM64BackgroundFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, PM64BackgroundHeaderExporter)
            REGISTER(Binary, PM64BackgroundBinaryExporter)
        };
    }
};
