#pragma once

#include "factories/BaseFactory.h"

#include <string>
#include <vector>

namespace AC {

enum EvwType : int16_t {
    EVW_SCROLL1 = 0,
    EVW_SCROLL2 = 1,
    EVW_COLREG_MANUAL = 2,
    EVW_COLREG_LINEAR = 3,
    EVW_COLREG_NONLINEAR = 4,
    EVW_TEXANIME = 5,
};

struct EvwEntry {
    int8_t segment;
    int16_t type;
    std::vector<uint8_t> scroll;
    uint16_t frameCount = 0;
    uint16_t keyCount = 0;
    std::vector<uint16_t> keyFrames;
    std::vector<uint8_t> prim;
    std::vector<uint8_t> env;
    std::vector<std::string> textures;
    std::vector<uint8_t> pattern;
};

class EvwAnimeData : public IParsedData {
public:
    std::vector<EvwEntry> mEntries;

    explicit EvwAnimeData(std::vector<EvwEntry> entries) : mEntries(std::move(entries)) {}
};

class EvwAnimeBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class EvwAnimeFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, EvwAnimeBinaryExporter)
        };
    }
};

} // namespace AC
