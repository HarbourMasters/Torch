#pragma once

#include "../BaseFactory.h"

namespace MK64 {

    struct YamlMetadata {
        std::string unk;
    };

    bool CompareCourseId(const CourseMetadata& a, const CourseMetadata& b);

    class MetadataData : public IParsedData {
    public:
        std::vector<YamlMetadata> mMetadata;

        explicit MetadataData(std::vector<YamlMetadata> metadata) : mMetadata(metadata) {}
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