#pragma once

#include "../BaseFactory.h"

namespace MK64 {

    struct BombKartSpawns {
        uint16_t waypointIndex;
        uint16_t startingState;
        float unk_04;
        float x;
        float z;
        float unk10;
        float unk14;
    };

    struct CourseMetadata {
        uint32_t courseId;
        std::string gCourseNames;
        std::string gDebugCourseNames;
        std::string gCupSelectionByCourseId;
        uint32_t gPerCupIndexByCourseId;
        std::string gWaypointWidth;
        std::string gWaypointWidth2;
        std::string D_800DCBB4;
        uint32_t gCPUSteeringSensitivity;
        std::vector<BombKartSpawns> bombKartSpawns;
        std::vector<uint16_t> gCoursePathSizes;
    };

    class MetadataData : public IParsedData {
    public:
        std::vector<CourseMetadata> mMetadata;

        explicit MetadataData(std::vector<CourseMetadata> metadata) : mMetadata(metadata) {}
    };

    class CourseMetadataHeaderExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class CourseMetadataBinaryExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class CourseMetadataCodeExporter : public BaseExporter {
        void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
    };

    class CourseMetadataFactory : public BaseFactory {
    public:
        std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
        inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
            return {
                REGISTER(Code, CourseMetadataCodeExporter)
                REGISTER(Header, CourseMetadataHeaderExporter)
                REGISTER(Binary, CourseMetadataBinaryExporter)
            };
        }
    };
}