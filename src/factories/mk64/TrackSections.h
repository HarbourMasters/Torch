#pragma once

#include "../BaseFactory.h"

namespace MK64 {

    struct TrackSections {
        uint32_t addr;
        uint8_t surfaceType;
        uint8_t sectionId;
        uint16_t flags;
    };

    class TrackSectionsData : public IParsedData {
    public:
        std::vector<TrackSections> mSecs;

        explicit TrackSectionsData(std::vector<TrackSections> sections) : mSecs(sections) {}
    };

    class TrackSectionsHeaderExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class TrackSectionsBinaryExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class TrackSectionsCodeExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class TrackSectionsFactory : public BaseFactory {
    public:
        std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
        inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
            return {
                REGISTER(Code, TrackSectionsCodeExporter)
                REGISTER(Header, TrackSectionsHeaderExporter)
                REGISTER(Binary, TrackSectionsBinaryExporter)
            };
        }
    };

}