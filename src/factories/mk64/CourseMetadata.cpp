#include "CourseMetadata.h"

#include "Companion.h"
#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"
#include <algorithm>

#define NUM(x) std::dec << std::setfill(' ') << std::setw(6) << x
#define COL(c) "0x" << std::hex << std::setw(2) << std::setfill('0') << c

void MK64::CourseMetadataHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    // const auto symbol = node["symbol"] ? node["symbol"].as<std::string>() : entryName;

    // if(Companion::Instance->IsOTRMode()){
    //     write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
    //     return;
    // }

    // write << "extern CourseMetadata " << symbol << "[];\n";
}

void MK64::CourseMetadataCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto metadata = std::static_pointer_cast<MetadataData>(raw)->mMetadata;
    //auto name = node["gCourseNames"].as<std::string>();

    if (metadata.empty()) {
        SPDLOG_ERROR("Course metadata null");
    }

    std::sort(metadata.begin(), metadata.end(),
        [this](const CourseMetadata& a, const CourseMetadata& b) {
            return a.courseId < b.courseId;
    });

    std::ofstream file;

    // // Sort metadata array by course id
    // Companion::Instance->SortCourseMetadata();

    file.open("gCourseNames.inc.c");
    if (file.is_open()) {
        file << "char *gCourseNames[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gCourseNames << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();

    file.open("gDebugCourseNames.inc.c");
    if (file.is_open()) {
        file << "char *gDebugCourseNames[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gDebugCourseNames << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();

    file.open("gCupSelectionByCourseId.inc.c");
    if (file.is_open()) {
        file << "char *gCupSelectionByCourseId[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gCupSelectionByCourseId << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();

    file.open("gPerCupIndexByCourseId.inc.c");
    if (file.is_open()) {
        file << "const u8 gPerCupIndexByCourseId[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gPerCupIndexByCourseId << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();

    file.open("gWaypointWidth.inc.c");
    if (file.is_open()) {
        file << "f32 gWaypointWidth[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gWaypointWidth << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();

    file.open("gWaypointWidth2.inc.c");
    if (file.is_open()) {
        file << "f32 gWaypointWidth2[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gWaypointWidth2 << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();

    file.open("D_800DCBB4.inc.c");
    if (file.is_open()) {
        file << "uintptr_t *D_800DCBB4[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.D_800DCBB4 << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();

    file.open("gCPUSteeringSensitivity.inc.c");
    if (file.is_open()) {
        file << "u16 gCPUSteeringSensitivity[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gCPUSteeringSensitivity << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();
}

void MK64::CourseMetadataBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    //auto metadata = std::static_pointer_cast<MetadataData>(raw)->mMetadata;
    //auto writer = LUS::BinaryWriter();

    SPDLOG_ERROR("CourseMetadata not implemented for OTR");

    // WriteHeader(writer, LUS::ResourceType::Metadata, 0);
    // writer.Write((uint32_t) metadata.size());
    //writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> MK64::CourseMetadataFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto dir = node["dir"].as<std::string>();
    SPDLOG_INFO("RUN");

    auto m = Companion::Instance->GetCourseMetadata();

    SPDLOG_INFO("RUN2");
    //const auto &metadata = m;

    std::vector<CourseMetadata> yamlData;
    for (const auto &metadata : m[dir]) {

        SPDLOG_INFO("RUN3");

        auto id = metadata["mario_raceway"]["courseId"].as<uint32_t>();
        SPDLOG_INFO("RUN4");
        auto name = metadata["name"].as<std::string>();
        auto debugName = metadata["debug_name"].as<std::string>();
        auto cup = metadata["cup"].as<std::string>();
        auto cupIndex = metadata["cupIndex"].as<uint32_t>();
        auto waypointWidth = metadata["waypoint_width"].as<std::string>();
        auto waypointWidth2 = metadata["waypoint_width2"].as<std::string>();

        auto D_800DCBB4 = metadata["D_800DCBB4"].as<std::string>();
        SPDLOG_INFO("RUN5");
        auto steeringSensitivity = metadata["cpu_steering_sensitivity"].as<uint32_t>();
        SPDLOG_INFO("RUN6");

        CourseMetadata data;

        data.courseId = id;
        data.gCourseNames = name;
        data.gDebugCourseNames = debugName;
        data.gCupSelectionByCourseId = cup;
        data.gPerCupIndexByCourseId = cupIndex;

        data.gWaypointWidth = waypointWidth;
        data.gWaypointWidth2 = waypointWidth2;

        data.D_800DCBB4 = D_800DCBB4;
        data.gCPUSteeringSensitivity = steeringSensitivity;


        yamlData.push_back(CourseMetadata(
            {id, name, debugName, cup, cupIndex, waypointWidth, waypointWidth2, D_800DCBB4, steeringSensitivity}
        ));
    }

    return std::make_shared<MetadataData>(yamlData);
}