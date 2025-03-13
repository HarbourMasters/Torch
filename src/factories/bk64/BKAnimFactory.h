#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef union {
    struct {
        uint16_t unk0_15 : 1;
        uint16_t unk0_14 : 1;
        uint16_t unk0_13 : 14;
        int16_t unk2;
    };
} AnimationFileData;

typedef struct animation_file_elem_s {
    uint16_t unk0_15 : 12; // start frame of animation?
    uint16_t unk0_3 : 4;   // end frame of animation?
    int16_t data_cnt;     //element count?
    AnimationFileData data[];
} AnimationFileElement;

int32_t animation_list_offset_18;

AnimationFileElement aData;

class AnimData : public IParsedData {
  public:

    AnimationFileElement aData;

    AnimData() = default;

    AnimData(AnimationFileElement aData) : aData(aData) {}
};

class AnimHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Code, AnimCodeExporter) REGISTER(Header, AnimHeaderExporter)
                     REGISTER(Binary, AnimBinaryExporter) };
    }
};
} 