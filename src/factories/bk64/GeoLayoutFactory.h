#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef std::variant<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, float> GeoLayoutArg;

enum class GeoLayoutArgType { U8, S8, U16, S16, U32, S32, FLOAT };

enum class GeoLayoutOpCode {
    UnknownCmd0,
    Sort,
    Bone,
    LoadDL,
    Skinning = 5,
    Branch,
    UnknownCmd7,
    LOD,
    ReferencePoint = 10,
    Selector = 12,
    DrawDistance,
    UnknownCmdE,
    UnknownCmdF,
    UnknownCmd10,
};

class GeoLayoutCommand {
  public:
    GeoLayoutOpCode opCode;
    uint32_t cmdLength;
    std::vector<GeoLayoutArg> args;
    uint32_t originalOffset; // where this command sat in the original N64 binary

    GeoLayoutCommand(GeoLayoutOpCode opCode, uint32_t cmdLength, std::vector<GeoLayoutArg> args,
                     uint32_t originalOffset = 0)
        : opCode(opCode), cmdLength(cmdLength), args(std::move(args)), originalOffset(originalOffset) {
    }
};

class GeoLayoutData : public IParsedData {
  public:
    std::vector<GeoLayoutCommand> mCmds;

    GeoLayoutData(std::vector<GeoLayoutCommand> cmds) : mCmds(std::move(cmds)) {
    }
};

class GeoLayoutHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class GeoLayoutBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class GeoLayoutCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class GeoLayoutModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class GeoLayoutFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    // std::optional<std::shared_ptr<IParsedData>>
    // parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, GeoLayoutCodeExporter) REGISTER(Header, GeoLayoutHeaderExporter)
                     REGISTER(Binary, GeoLayoutBinaryExporter) REGISTER(Modding, GeoLayoutModdingExporter) };
    }
    bool SupportModdedAssets() override {
        return true;
    }
};
} // namespace BK64
