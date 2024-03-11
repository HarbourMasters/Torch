#include "CourseMetadata.h"

#include "Companion.h"
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

    if (metadata.empty()) {
        throw std::runtime_error("Course metadata null");
    }

    // Sort the data by id, 0 to 20 and beyond.
    std::sort(metadata.begin(), metadata.end(),
        [this](const CourseMetadata& a, const CourseMetadata& b) {
            return a.id < b.id;
    });

    std::ofstream file;
    std::string outDir = "assets/course_data/";

    file.open(outDir+"gCourseNames.inc.c");
    if (file.is_open()) {
       // file << "char *gCourseNames[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            // Remove debug line once proven that sort worked right (start at id 0 and go up)
            SPDLOG_INFO("Processing Course Id: "+std::to_string(m.id));
            file << '"' << m.name << "\", ";
        }
       // file << "\n};\n\n";
        file.close();
    } else if (file.fail()) {
        throw std::runtime_error("Course metadata output folder is likely bad or the file is in-use");
    }

    file.open(outDir+"gDebugCourseNames.inc.c");
    if (file.is_open()) {
       // file << "char *gDebugCourseNames[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << '"' << m.debugName << "\", ";
        }
        //file << "\n};\n\n";
    }
    file.close();

    file.open(outDir+"gCupSelectionByCourseId.inc.c");
    if (file.is_open()) {
        //file << "char *gCupSelectionByCourseId[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.cup << ", ";
        }
       // file << "\n};\n\n";
    }
    file.close();

    file.open(outDir+"gPerCupIndexByCourseId.inc.c");
    if (file.is_open()) {
        //file << "const u8 gPerCupIndexByCourseId[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.cupIndex << ", ";
        }
        //file << "\n};\n\n";
    }
    file.close();

    file.open(outDir+"gWaypointWidth.inc.c");
    if (file.is_open()) {
       // file << "f32 gWaypointWidth[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.waypointWidth << ", ";
        }
       // file << "\n};\n\n";
    }
    file.close();

    file.open(outDir+"gWaypointWidth2.inc.c");
    if (file.is_open()) {
        file << "f32 gWaypointWidth2[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.waypointWidth2 << ", ";
        }
        file << "\n};\n\n";
    }
    file.close();

    file.open(outDir+"D_800DCBB4.inc.c");
    if (file.is_open()) {
        //file << "uintptr_t *D_800DCBB4[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.D_800DCBB4 << ", ";
        }
        //file << "\n};\n\n";
    }
    file.close();

    file.open(outDir+"gCPUSteeringSensitivity.inc.c");
    if (file.is_open()) {
        //file << "u16 gCPUSteeringSensitivity[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << m.steeringSensitivity << ", ";
        }
        //file << "\n};\n\n";
    }
    file.close();

    file.open(outDir+"gCourseBombKartSpawns.inc.c");
    if (file.is_open()) {
        //file << "u16 gCPUSteeringSensitivity[] = {\n" << fourSpaceTab;
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
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

    file.open(outDir+"gCoursePathSizes.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto& size : m.pathSizes) {
                file << size << ", ";
            }
            file << "},\n";
        }
        file.close();
    }

    file.open(outDir+"D_0D009418.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto size : m.D_0D009418) {
                file << size << ", ";
            }
            file << "},\n";
        }
        file.close();
    }

    file.open(outDir+"D_0D009568.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto& size : m.D_0D009568) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open(outDir+"D_0D0096B8.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto& size : m.D_0D0096B8) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open(outDir+"D_0D009808.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto& size : m.D_0D009808) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open(outDir+"gCoursePathTable.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto& size : m.pathTable) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open(outDir+"gCoursePathTableUnknown.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto& size : m.pathTableUnknown) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open(outDir+"sSkyColors.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto& size : m.skyColors) {
                file << size << ", ";
            }
            file << "},\n";
        }
    }
    file.close();

    file.open(outDir+"sSkyColors2.inc.c");
    if (file.is_open()) {
        for (const auto& m : metadata) {
            file << "// " << m.name << "\n";
            file << "{ ";
            for (const auto& size : m.skyColors2) {
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

    throw std::runtime_error("CourseMetadata not implemented for OTR");

    // WriteHeader(writer, LUS::ResourceType::Metadata, 0);
    // writer.Write((uint32_t) metadata.size());
    //writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> MK64::CourseMetadataFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto dir = GetSafeNode<std::string>(node, "dir");
 
    auto m = Companion::Instance->GetCourseMetadata();

    std::vector<CourseMetadata> yamlData;
    for (const auto &yamls : m[dir]) {
        
        if (!yamls["course"]) {
            throw std::runtime_error("Course yaml missing root label of course\nEx. course:");
        }

        auto metadata = yamls["course"];

        CourseMetadata data;

        data.id =                   GetSafeNode<uint32_t>(metadata, "id");
        data.name =                 GetSafeNode<std::string>(metadata, "name");
        data.debugName =            GetSafeNode<std::string>(metadata, "debug_name");
        data.cup =                  GetSafeNode<std::string>(metadata, "cup");
        data.cupIndex =             GetSafeNode<uint32_t>(metadata, "cup_index");

        data.waypointWidth =        GetSafeNode<std::string>(metadata, "waypoint_width");
        data.waypointWidth2 =       GetSafeNode<std::string>(metadata, "waypoint_width2");

        data.D_800DCBB4 =           GetSafeNode<std::string>(metadata, "D_800DCBB4");
        data.steeringSensitivity =  GetSafeNode<uint32_t>(metadata, "cpu_steering_sensitivity");

        for (const auto& bombKart : GetSafeNode<YAML::Node>(metadata, "bomb_kart_spawns")) {
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

        for (const auto& size : GetSafeNode<YAML::Node>(metadata, "path_sizes")) {
            data.pathSizes.push_back(size.as<uint16_t>());
        }

        for (const auto& value : GetSafeNode<YAML::Node>(metadata, "D_0D009418")) {
            data.D_0D009418.push_back(value.as<std::string>());
        }

        for (const auto& value : GetSafeNode<YAML::Node>(metadata, "D_0D009568")) {
            data.D_0D009568.push_back(value.as<std::string>());
        }

        for (const auto& value : GetSafeNode<YAML::Node>(metadata, "D_0D0096B8")) {
            data.D_0D0096B8.push_back(value.as<std::string>());
        }

        for (const auto& value : GetSafeNode<YAML::Node>(metadata, "D_0D009808")) {
            data.D_0D009808.push_back(value.as<std::string>());
        }

        for (const auto& str : GetSafeNode<YAML::Node>(metadata, "path_table")) {
            data.pathTable.push_back(str.as<std::string>());
        }

        for (const auto& str : GetSafeNode<YAML::Node>(metadata, "path_table_unknown")) {
            data.pathTableUnknown.push_back(str.as<std::string>());
        }

        for (const auto& value : GetSafeNode<YAML::Node>(metadata, "sky_colors")) {
            data.skyColors.push_back(value.as<int16_t>());
        }

        for (const auto& value : GetSafeNode<YAML::Node>(metadata, "sky_colors2")) {
            data.skyColors2.push_back(value.as<int16_t>());
        }

        yamlData.push_back(CourseMetadata(
            {data}
        ));
    }

    return std::make_shared<MetadataData>(yamlData);
}