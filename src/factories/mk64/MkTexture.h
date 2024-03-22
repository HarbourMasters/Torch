#pragma once

#include "../BaseFactory.h"

enum class MkTextureType {
    Error,
    RGBA32bpp,
    RGBA16bpp,
    Palette4bpp,
    Palette8bpp,
    Grayscale4bpp,
    Grayscale8bpp,
    GrayscaleAlpha1bpp,
    GrayscaleAlpha4bpp,
    GrayscaleAlpha8bpp,
    GrayscaleAlpha16bpp,
    TLUT
};

struct MkTextureFormat {
    MkTextureType type;
    uint32_t depth;
};

class MkTextureData : public IParsedData {
public:
    MkTextureFormat mFormat;
    uint32_t mWidth;
    uint32_t mHeight;
    std::vector<uint8_t> mBuffer;

    MkTextureData(MkTextureFormat format, uint32_t width, uint32_t height, std::vector<uint8_t>& buffer) : mFormat(format), mWidth(width), mHeight(height), mBuffer(std::move(buffer)) {}
};

class MkTextureHeaderExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MkTextureCodeExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MkTextureBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MkTextureModdingExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class MkTextureFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, MkTextureHeaderExporter)
            REGISTER(Binary, MkTextureBinaryExporter)
            REGISTER(Code, MkTextureCodeExporter)
            REGISTER(Modding, MkTextureModdingExporter)
        };
    }
    bool SupportModdedAssets() override { return true; }
};