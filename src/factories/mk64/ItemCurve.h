#pragma once

#include "../BaseFactory.h"

namespace MK64 {

    enum {
    /* 0x00 */ ITEM_NONE = 0,
    /* 0x01 */ ITEM_BANANA,
    /* 0x02 */ ITEM_BANANA_BUNCH,
    /* 0x03 */ ITEM_GREEN_SHELL,
    /* 0x04 */ ITEM_TRIPLE_GREEN_SHELL,
    /* 0x05 */ ITEM_RED_SHELL,
    /* 0x06 */ ITEM_TRIPLE_RED_SHELL,
    /* 0x07 */ ITEM_BLUE_SPINY_SHELL,
    /* 0x08 */ ITEM_THUNDERBOLT,
    /* 0x09 */ ITEM_FAKE_ITEM_BOX,
    /* 0x0A */ ITEM_STAR,
    /* 0x0B */ ITEM_BOO,
    /* 0x0C */ ITEM_MUSHROOM,
    /* 0x0D */ ITEM_DOUBLE_MUSHROOM,
    /* 0x0E */ ITEM_TRIPLE_MUSHROOM,
    /* 0x0F */ ITEM_SUPER_MUSHROOM
    };

    class ItemCurveData : public IParsedData {
    public:
        std::vector<uint8_t> mItems;

        explicit ItemCurveData(std::vector<uint8_t> items) : mItems(items) {}
    };

    class ItemCurveHeaderExporter : public BaseExporter {
        ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class ItemCurveBinaryExporter : public BaseExporter {
        ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class ItemCurveCodeExporter : public BaseExporter {
        ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class ItemCurveFactory : public BaseFactory {
    public:
        std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
        std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
            return std::nullopt;
        }
        inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
            return {
                REGISTER(Code, ItemCurveCodeExporter)
                REGISTER(Header, ItemCurveHeaderExporter)
                REGISTER(Binary, ItemCurveBinaryExporter)
            };
        }
        bool SupportModdedAssets() override { return false; }
    };

}