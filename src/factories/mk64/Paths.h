#pragma once

#include <factories/BaseFactory.h>

namespace MK64 {

struct TrackPath {
    int16_t posX;
    int16_t posY;
    int16_t posZ;
    uint16_t trackSegment;
};

class PathData : public IParsedData {
  public:
    std::vector<TrackPath> mPaths;

    explicit PathData(std::vector<TrackPath> paths) : mPaths(paths) {
    }
};

class PathHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PathBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PathCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PathsFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, PathCodeExporter) REGISTER(Header, PathHeaderExporter)
                     REGISTER(Binary, PathBinaryExporter) };
    }
    bool SupportModdedAssets() override {
        return false;
    }
};
} // namespace MK64