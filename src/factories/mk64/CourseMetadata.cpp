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
       // file << "char *gCourseNames[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            // Remove debug line once proven that sort worked right (start at id 0 and go up)
            SPDLOG_INFO("Processing Course Id: "+std::to_string(m.courseId));
            file << '"' << m.gCourseNames << "\", ";
        }
       // file << "\n};\n\n";
    }
    file.close();

    file.open("gDebugCourseNames.inc.c");
    if (file.is_open()) {
       // file << "char *gDebugCourseNames[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << '"' << m.gDebugCourseNames << "\", ";
        }
        //file << "\n};\n\n";
    }
    file.close();

    file.open("gCupSelectionByCourseId.inc.c");
    if (file.is_open()) {
        //file << "char *gCupSelectionByCourseId[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gCupSelectionByCourseId << ", ";
        }
       // file << "\n};\n\n";
    }
    file.close();

    file.open("gPerCupIndexByCourseId.inc.c");
    if (file.is_open()) {
        //file << "const u8 gPerCupIndexByCourseId[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gPerCupIndexByCourseId << ", ";
        }
        //file << "\n};\n\n";
    }
    file.close();

    file.open("gWaypointWidth.inc.c");
    if (file.is_open()) {
       // file << "f32 gWaypointWidth[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gWaypointWidth << ", ";
        }
       // file << "\n};\n\n";
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
        //file << "uintptr_t *D_800DCBB4[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.D_800DCBB4 << ", ";
        }
        //file << "\n};\n\n";
    }
    file.close();

    file.open("gCPUSteeringSensitivity.inc.c");
    if (file.is_open()) {
        //file << "u16 gCPUSteeringSensitivity[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.gCPUSteeringSensitivity << ", ";
        }
        //file << "\n};\n\n";
    }
    file.close();

    file.open("gCourseBombKartSpawns.inc.c");
    if (file.is_open()) {
        //file << "u16 gCPUSteeringSensitivity[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            for (const auto& bombKart : m.bombKartSpawns) {
                file << "{ ";
                file << bombKart.waypointIndex << ", ";
                file << bombKart.startingState << ", ";
                file << bombKart.unk_04 << ", ";
                file << bombKart.x << ", ";
                file << bombKart.z << ", ";
                file << bombKart.unk10 << ", ";
                file << bombKart.unk14;
                file << " },\n";
            }
        }
    }
    file.close();

    file.open("gCoursePathSizes.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.gCoursePathSizes) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open("D_0D009418.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.D_0D009418) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open("D_0D009568.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.D_0D009568) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open("D_0D0096B8.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.D_0D0096B8) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open("D_0D009808.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.D_0D009808) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open("gCoursePathTable.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.gCoursePathTable) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open("gCoursePathTableUnknown.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.gCoursePathTableUnknown) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open("sSkyColors.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.sSkyColors) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open("sSkyColors2.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.gCourseNames << "\n";
            file << "{ ";
            for (const auto& size : m.sSkyColors2) {
                file << size << ", ";
            }
            file << "},\n";
        }
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

    //const auto &metadata = m;

    std::vector<CourseMetadata> yamlData;
    for (const auto &yamls : m[dir]) {
        const auto& metadata = yamls["course"];

        auto id = metadata["courseId"].as<uint32_t>();
        auto name = metadata["name"].as<std::string>();
        auto debugName = metadata["debug_name"].as<std::string>();
        auto cup = metadata["cup"].as<std::string>();
        auto cupIndex = metadata["cupIndex"].as<uint32_t>();
        auto waypointWidth = metadata["waypoint_width"].as<std::string>();
        auto waypointWidth2 = metadata["waypoint_width2"].as<std::string>();

        auto D_800DCBB4 = metadata["D_800DCBB4"].as<std::string>();

        auto steeringSensitivity = metadata["cpu_steering_sensitivity"].as<uint32_t>();

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

        for (const auto& bombKart : metadata["gCourseBombKartSpawns"]) {
            data.bombKartSpawns.push_back(BombKartSpawns({
                bombKart[0].as<uint16_t>(),
                bombKart[1].as<uint16_t>(),
                bombKart[2].as<float>(),
                bombKart[3].as<float>(),
                bombKart[4].as<float>(),
                bombKart[5].as<float>(),
                bombKart[6].as<float>(),
            }));
        }

    SPDLOG_INFO("RUN2");
        for (const auto& size : metadata["gCoursePathSizes"]) {
            data.gCoursePathSizes.push_back(size.as<uint16_t>());
        }
SPDLOG_INFO("RUN3");
        for (const auto& value : metadata["D_0D009418"]) {
            data.D_0D009418.push_back(value.as<float>());
        }
SPDLOG_INFO("RUN4");
        for (const auto& value : metadata["D_0D009568"]) {
            data.D_0D009568.push_back(value.as<float>());
        }

        for (const auto& value : metadata["D_0D0096B8"]) {
            data.D_0D0096B8.push_back(value.as<float>());
        }

        for (const auto& value : metadata["D_0D009808"]) {
            data.D_0D009808.push_back(value.as<float>());
        }

        for (const auto& str : metadata["gCoursePathTable"]) {
            data.gCoursePathTable.push_back(str.as<std::string>());
        }

        for (const auto& str : metadata["gCoursePathTableUnknown"]) {
            data.gCoursePathTableUnknown.push_back(str.as<std::string>());
        }

        for (const auto& value : metadata["sSkyColors"]) {
            data.sSkyColors.push_back(value.as<int16_t>());
        }

        for (const auto& value : metadata["sSkyColors2"]) {
            data.sSkyColors2.push_back(value.as<int16_t>());
        }

        yamlData.push_back(CourseMetadata(
            {data}
        ));
    }

    return std::make_shared<MetadataData>(yamlData);
}