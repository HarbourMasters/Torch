#include "Companion.h"

#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include "storm/SWrapper.h"
#include "spdlog/spdlog.h"
#include "hj/sha1.h"

#include <regex>
#include <fstream>
#include <iostream>
#include <filesystem>

#include "factories/BankFactory.h"
#include "factories/AudioHeaderFactory.h"
#include "factories/SampleFactory.h"
#include "factories/SequenceFactory.h"
#include "factories/GenericArrayFactory.h"
#include "factories/VtxFactory.h"
#include "factories/MtxFactory.h"
#include "factories/FloatFactory.h"
#include "factories/IncludeFactory.h"
#include "factories/TextureFactory.h"
#include "factories/DisplayListFactory.h"
#include "factories/DisplayListOverrides.h"
#include "factories/BlobFactory.h"
#include "factories/LightsFactory.h"
#include "factories/Vec3fFactory.h"
#include "factories/Vec3sFactory.h"

#include "factories/sm64/AnimationFactory.h"
#include "factories/sm64/CollisionFactory.h"
#include "factories/sm64/DialogFactory.h"
#include "factories/sm64/DictionaryFactory.h"
#include "factories/sm64/TextFactory.h"
#include "factories/sm64/GeoLayoutFactory.h"

#include "factories/mk64/CourseVtx.h"
#include "factories/mk64/Waypoints.h"
#include "factories/mk64/TrackSections.h"
#include "factories/mk64/SpawnData.h"
#include "factories/mk64/DrivingBehaviour.h"
#include "factories/mk64/ItemCurve.h"
#include "factories/mk64/CourseMetadata.h"

#include "factories/sf64/ColPolyFactory.h"
#include "factories/sf64/MessageFactory.h"
#include "factories/sf64/MessageLookupFactory.h"
#include "factories/sf64/SkeletonFactory.h"
#include "factories/sf64/AnimFactory.h"
#include "factories/sf64/ScriptFactory.h"
#include "factories/sf64/HitboxFactory.h"
#include "factories/sf64/EnvSettingsFactory.h"
#include "factories/sf64/ObjInitFactory.h"
#include "factories/sf64/TriangleFactory.h"

using namespace std::chrono;
namespace fs = std::filesystem;

static const std::string regular = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";
static const std::string line    = "[%Y-%m-%d %H:%M:%S.%e] [%l] > %v";

#define ABS(x) ((x) < 0 ? -(x) : (x))

void Companion::Init(const ExportType type) {

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    this->gConfig.exporterType = type;
    this->RegisterFactory("BLOB", std::make_shared<BlobFactory>());
    this->RegisterFactory("TEXTURE", std::make_shared<TextureFactory>());
    this->RegisterFactory("VTX", std::make_shared<VtxFactory>());
    this->RegisterFactory("MTX", std::make_shared<MtxFactory>());
    this->RegisterFactory("F32", std::make_shared<FloatFactory>());
    this->RegisterFactory("INC", std::make_shared<IncludeFactory>());
    this->RegisterFactory("LIGHTS", std::make_shared<LightsFactory>());
    this->RegisterFactory("GFX", std::make_shared<DListFactory>());
    this->RegisterFactory("AUDIO:HEADER", std::make_shared<AudioHeaderFactory>());
    this->RegisterFactory("SEQUENCE", std::make_shared<SequenceFactory>());
    this->RegisterFactory("SAMPLE", std::make_shared<SampleFactory>());
    this->RegisterFactory("BANK", std::make_shared<BankFactory>());
    this->RegisterFactory("VEC3F", std::make_shared<Vec3fFactory>());
    this->RegisterFactory("VEC3S", std::make_shared<Vec3sFactory>());
    this->RegisterFactory("ARRAY", std::make_shared<GenericArrayFactory>());

    // SM64 specific
    this->RegisterFactory("SM64:DIALOG", std::make_shared<SM64::DialogFactory>());
    this->RegisterFactory("SM64:TEXT", std::make_shared<SM64::TextFactory>());
    this->RegisterFactory("SM64:DICTIONARY", std::make_shared<SM64::DictionaryFactory>());
    this->RegisterFactory("SM64:ANIM", std::make_shared<SM64::AnimationFactory>());
    this->RegisterFactory("SM64:COLLISION", std::make_shared<SM64::CollisionFactory>());
    this->RegisterFactory("SM64:GEO_LAYOUT", std::make_shared<SM64::GeoLayoutFactory>());

    // MK64 specific
    this->RegisterFactory("MK64:COURSE_VTX", std::make_shared<MK64::CourseVtxFactory>());
    this->RegisterFactory("MK64:TRACK_WAYPOINTS", std::make_shared<MK64::WaypointsFactory>());
    this->RegisterFactory("MK64:TRACK_SECTIONS", std::make_shared<MK64::TrackSectionsFactory>());
    this->RegisterFactory("MK64:SPAWN_DATA", std::make_shared<MK64::SpawnDataFactory>());
    this->RegisterFactory("MK64:DRIVING_BEHAVIOUR", std::make_shared<MK64::DrivingBehaviourFactory>());
    this->RegisterFactory("MK64:ITEM_CURVE", std::make_shared<MK64::ItemCurveFactory>()); // Item curve for decomp only
    this->RegisterFactory("MK64:METADATA", std::make_shared<MK64::CourseMetadataFactory>());

    // SF64 specific
    this->RegisterFactory("SF64:ANIM", std::make_shared<SF64::AnimFactory>());
    this->RegisterFactory("SF64:SKELETON", std::make_shared<SF64::SkeletonFactory>());
    this->RegisterFactory("SF64:MESSAGE", std::make_shared<SF64::MessageFactory>());
    this->RegisterFactory("SF64:MSG_TABLE", std::make_shared<SF64::MessageLookupFactory>());
    this->RegisterFactory("SF64:SCRIPT", std::make_shared<SF64::ScriptFactory>());
    this->RegisterFactory("SF64:HITBOX", std::make_shared<SF64::HitboxFactory>());
    this->RegisterFactory("SF64:ENV_SETTINGS", std::make_shared<SF64::EnvSettingsFactory>());
    this->RegisterFactory("SF64:OBJECT_INIT", std::make_shared<SF64::ObjInitFactory>());
    this->RegisterFactory("SF64:COLPOLY", std::make_shared<SF64::ColPolyFactory>());
    this->RegisterFactory("SF64:TRIANGLE", std::make_shared<SF64::TriangleFactory>());

    this->Process();
}

void Companion::ParseEnums(std::string& header) {
    std::ifstream file(header);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open header files for enums node in config");
    }

    std::regex enumRegex(R"(enum\s+(\w+)\s*(?:\s*:\s*(\w+))?[\s\n\r]*\{)");

    std::string line;
    std::smatch match;
    std::string enumName;

    bool inEnum = false;
    int enumIndex;
    while (std::getline(file, line)) {
        if (!inEnum && std::regex_search(line, match, enumRegex) && match.size() > 1) {
            enumName = match.str(1);
            inEnum = true;
            enumIndex = -1;
            continue;
        }

        if(!inEnum) {
            continue;
        }

        if(line.find("}") != std::string::npos) {
            inEnum = false;
            continue;
        }

        // Remove any comments and non-alphanumeric characters
        line = std::regex_replace(line, std::regex(R"((/\*.*?\*/)|(//.*$)|([^a-zA-Z0-9=_\-\.]))"), "");

        if(line.find("=") != std::string::npos) {
            auto value = line.substr(line.find("=") + 1);
            auto name = line.substr(0, line.find("="));
            enumIndex = std::stoi(value);
            this->gEnums[enumName][enumIndex] = name;
        } else {
            enumIndex++;
            this->gEnums[enumName][enumIndex] = line;
        }

    }
}

void Companion::ExtractNode(YAML::Node& node, std::string& name, SWrapper* binary) {
    std::ostringstream stream;

    this->gCurrentWrapper = binary;

    auto type = GetSafeNode<std::string>(node, "type");
    std::transform(type.begin(), type.end(), type.begin(), ::toupper);

    spdlog::set_pattern(regular);
    if(node["offset"]) {
        auto offset = node["offset"].as<uint32_t>();
        SPDLOG_INFO("- [{}] Processing {} at 0x{:X}", type, name, offset);
    } else {
        SPDLOG_INFO("- [{}] Processing {}", type, name);
    }
    spdlog::set_pattern(line);

    auto factory = this->GetFactory(type);
    if(!factory.has_value()){
        throw std::runtime_error("No factory by the name '"+type+"' found for '"+name+"'");
        return;
    }


    auto impl = factory->get();

    auto exporter = impl->GetExporter(this->gConfig.exporterType);
    if(!exporter.has_value()){
        SPDLOG_WARN("No exporter found for {}", name);
        return;
    }

    std::optional<std::shared_ptr<IParsedData>> result;
    if(this->gConfig.modding && impl->SupportModdedAssets() && this->gModdedAssetPaths.contains(name)) {
        auto path = fs::path(this->gConfig.moddingPath) / this->gModdedAssetPaths[name];
        if(!fs::exists(path)) {
            SPDLOG_ERROR("Modded asset {} not found", this->gModdedAssetPaths[name]);
            return;
        }

        std::ifstream input(path, std::ios::binary);
        std::vector<uint8_t> data = std::vector<uint8_t>( std::istreambuf_iterator( input ), {});
        input.close();

        result = impl->parse_modding(data, node);
    } else {
        result = impl->parse(this->gRomData, node);
    }

    if(!result.has_value()){
        SPDLOG_ERROR("Failed to process {}", name);
        return;
    }

    for (auto [fst, snd] : this->gAssetDependencies[this->gCurrentFile]) {
        if(snd.second) {
            continue;
        }
        std::string doutput = (this->gCurrentDirectory / fst).string();
        std::replace(doutput.begin(), doutput.end(), '\\', '/');
        this->gAssetDependencies[this->gCurrentFile][fst].second = true;
        this->ExtractNode(snd.first, doutput, binary);
        spdlog::set_pattern(regular);
        SPDLOG_INFO("------------------------------------------------");
        spdlog::set_pattern(line);
    }

    ExportResult endptr = std::nullopt;

    switch (this->gConfig.exporterType) {
        case ExportType::Binary: {
            if(binary == nullptr)  {
                break;
            }

            stream.str("");
            stream.clear();
            exporter->get()->Export(stream, result.value(), name, node, &name);
            auto data = stream.str();
            binary->CreateFile(name, std::vector(data.begin(), data.end()));
            break;
        }
        case ExportType::Modding: {
            stream.str("");
            stream.clear();
            std::string ogname = name;
            exporter->get()->Export(stream, result.value(), name, node, &name);

            auto data = stream.str();
            if(data.empty()) {
                break;
            }

            std::string dpath = Instance->GetOutputPath() + "/" + name;
            if(!exists(fs::path(dpath).parent_path())){
                create_directories(fs::path(dpath).parent_path());
            }

            this->gModdedAssetPaths[ogname] = name;

            std::ofstream file(dpath, std::ios::binary);
            file.write(data.c_str(), data.size());
            file.close();
            break;
        }
        default: {
            endptr = exporter->get()->Export(stream, result.value(), name, node, &name);
            break;
        }
    }

    SPDLOG_INFO("Processed {}", name);
    WriteEntry entry;
    if(node["offset"]) {
        auto alignment = GetSafeNode<uint32_t>(node, "alignment", impl->GetAlignment());
        if(!endptr.has_value()) {
            entry = {
                node["offset"].as<uint32_t>(),
                alignment,
                stream.str(),
                std::nullopt
            };
        } else {
            switch (endptr->index()) {
                case 0:
                    entry = {
                        node["offset"].as<uint32_t>(),
                        alignment,
                        stream.str(),
                        std::get<size_t>(endptr.value())
                    };
                    break;
                case 1: {
                    const auto oentry = std::get<OffsetEntry>(endptr.value());
                    entry = {
                        oentry.start,
                        alignment,
                        stream.str(),
                        oentry.end
                    };
                    break;
                }
                default:
                    SPDLOG_ERROR("Invalid endptr index {}", endptr->index());
                    SPDLOG_ERROR("Type of endptr: {}", typeid(endptr).name());
                    throw std::runtime_error("We should never reach this point");
            }
        }
    }
    this->gWriteMap[this->gCurrentFile][type].push_back(entry);
}

void Companion::ParseModdingConfig() {
    auto path = fs::path(this->gConfig.moddingPath) / "modding.yml";
    if(!fs::exists(path)) {
        throw std::runtime_error("No modding config found, please run in export mode first");
    }
    auto modding = YAML::LoadFile(path.string());
    for(auto assets = modding["assets"].begin(); assets != modding["assets"].end(); ++assets) {
        auto name = assets->first.as<std::string>();
        auto asset = assets->second.as<std::string>();
        this->gModdedAssetPaths[name] = asset;
    }
}


void Companion::ParseCurrentFileConfig(YAML::Node node) {
    if(node["segments"]) {
        auto segments = node["segments"];

        // Set global variables for segmented data
        if (segments.IsSequence() && segments.size()) {
            if (segments[0].IsSequence() && segments[0].size() == 2) {
                gCurrentSegmentNumber = segments[0][0].as<uint32_t>();
                gCurrentFileOffset = segments[0][1].as<uint32_t>();
                gCurrentCompressionType = GetCompressionType(this->gRomData, gCurrentFileOffset);
            } else {
                throw std::runtime_error("Incorrect yaml syntax for segments.\n\nThe yaml expects:\n:config:\n  segments:\n  - [<segment>, <file_offset>]\n\nLike so:\nsegments:\n  - [0x06, 0x821D10]");
            }
        }

        // Set file offset for later use.
        for(size_t i = 0; i < segments.size(); i++) {
            auto segment = segments[i];
            if (segment.IsSequence() && segment.size() == 2) {
                const auto id = segment[0].as<uint32_t>();
                const auto replacement = segment[1].as<uint32_t>();
                this->gConfig.segment.local[id] = replacement;
                SPDLOG_DEBUG("Segment {} replaced with 0x{:X}", id, replacement);
            } else {
                throw std::runtime_error("Incorrect yaml syntax for segments.\n\nThe yaml expects:\n:config:\n  segments:\n  - [<segment>, <file_offset>]\n\nLike so:\nsegments:\n  - [0x06, 0x821D10]");
            }
        }
    }

    if(node["header"]) {
        auto header = node["header"];
        switch (this->gConfig.exporterType) {
            case ExportType::Header: {
                if(header["header"].IsSequence()) {
                    for(auto line = header["header"].begin(); line != header["header"].end(); ++line) {
                        this->gFileHeader += line->as<std::string>() + "\n";
                    }
                }
                break;
            }
            case ExportType::Code: {
                if(header["code"].IsSequence()) {
                    for(auto line = header["code"].begin(); line != header["code"].end(); ++line) {
                        this->gFileHeader += line->as<std::string>() + "\n";
                    }
                }
                break;
            }
            default: break;
        }
    }

    if(node["tables"]){
        for(auto table = node["tables"].begin(); table != node["tables"].end(); ++table){
            auto name = table->first.as<std::string>();
            auto range = table->second["range"].as<std::vector<uint32_t>>();
            auto start = gCurrentSegmentNumber ? gCurrentSegmentNumber << 24 | range[0] : range[0];
            auto end = gCurrentSegmentNumber ? gCurrentSegmentNumber << 24 | range[1] : range[1];
            auto mode = GetSafeNode<std::string>(table->second, "mode", "APPEND");
            TableMode tMode = mode == "REFERENCE" ? TableMode::Reference : TableMode::Append;
            this->gTables.push_back({name, start, end, tMode});
        }
    }

    if(node["vram"]){
        auto vram = node["vram"];
        const auto addr = GetSafeNode<uint32_t>(vram, "addr");
        const auto offset = GetSafeNode<uint32_t>(vram, "offset");
        this->gCurrentVram = { addr, offset };
    }

    if (node["external_files"]) {
        auto externalFiles = node["external_files"];
        if (externalFiles.IsSequence() && externalFiles.size()) {
            for(size_t i = 0; i < externalFiles.size(); i++) {
                auto externalFile = externalFiles[i];
                if (externalFile.size() == 0) {
                    this->gCurrentExternalFiles.push_back(externalFile.as<std::string>());
                } else {
                    SPDLOG_INFO("External File size {}", externalFile.size());
                    throw std::runtime_error("Incorrect yaml syntax for external files.\n\nThe yaml expects:\n:config:\n  external_files:\n  - <external_files>\n\ne.g.:\nexternal_files:\n  - actors/actor1.yaml");
                }
                auto externalFileName = externalFile.as<std::string>();
                if (std::filesystem::relative(externalFileName, this->gAssetPath).string().starts_with("../")) {
                    throw std::runtime_error("External File " + externalFileName + " Not In Asset Directory " + this->gAssetPath);
                } else if (std::filesystem::relative(externalFileName, this->gAssetPath).string() == "") {
                    throw std::runtime_error("External File " + externalFileName + " Not In Asset Directory " + this->gAssetPath);
                }
                auto localCurrentDirectory = std::filesystem::relative(externalFileName, this->gAssetPath).replace_extension("");

                if (!this->gAddrMap.contains(externalFileName)) {
                    // Skim file to populate with nodes
                    YAML::Node root = YAML::LoadFile(externalFileName);
                    uint32_t localSegmentNumber = 0;
                    if (root[":config"]) {
                        YAML::Node node = root[":config"];
                        if(node["segments"]) {
                            auto segments = node["segments"];

                            // Set local variables for segmented data
                            if (segments.IsSequence() && segments.size()) {
                                if (segments[0].IsSequence() && segments[0].size() == 2) {
                                    localSegmentNumber = segments[0][0].as<uint32_t>();
                                } else {
                                    throw std::runtime_error("Incorrect yaml syntax for segments.\n\nThe yaml expects:\n:config:\n  segments:\n  - [<segment>, <file_offset>]\n\nLike so:\nsegments:\n  - [0x06, 0x821D10]");
                                }
                            }
                        }
                    }
                    for(auto asset = root.begin(); asset != root.end(); ++asset){
                        auto node = asset->second;
                        auto entryName = asset->first.as<std::string>();

                        // Parse horizontal assets
                        if(node["files"]){
                            auto segment = node["segment"] ? node["segment"].as<uint8_t>() : -1;
                            const auto files = node["files"];
                            for (const auto& file : files) {
                                auto assetNode = file.as<YAML::Node>();
                                auto childName = assetNode["name"].as<std::string>();
                                auto output = (localCurrentDirectory / entryName / childName).string();
                                std::replace(output.begin(), output.end(), '\\', '/');

                                if(!assetNode["offset"]) {
                                    continue;
                                }

                                if(segment != -1 || localSegmentNumber) {
                                    assetNode["offset"] = (segment << 24) | assetNode["offset"].as<uint32_t>();
                                }

                                this->gAddrMap[externalFileName][assetNode["offset"].as<uint32_t>()] = std::make_tuple(output, assetNode);
                            }
                        } else {
                            auto output = (localCurrentDirectory / entryName).string();
                            std::replace(output.begin(), output.end(), '\\', '/');

                            if(!node["offset"])  {
                                continue;
                            }

                            if(localSegmentNumber) {
                                if (IS_SEGMENTED(node["offset"].as<uint32_t>()) == false) {
                                    node["offset"] = (localSegmentNumber << 24) | node["offset"].as<uint32_t>();
                                }
                            }

                            this->gAddrMap[externalFileName][node["offset"].as<uint32_t>()] = std::make_tuple(output, node);
                        }
                    }
                }
            }
        }
    }

    this->gEnablePadGen = GetSafeNode<bool>(node, "autopads", true);
    this->gNodeForceProcessing = GetSafeNode<bool>(node, "force", false);
}

void Companion::ParseHash() {
    const std::string out = "torch.hash.yml";

    if(fs::exists(out)) {
        this->gHashNode = YAML::LoadFile(out);
    } else {
        this->gHashNode = YAML::Node();
    }
}

std::string ExportTypeToString(ExportType type) {
    switch (type) {
        case ExportType::Binary: return "Binary";
        case ExportType::Header: return "Header";
        case ExportType::Code: return "Code";
        case ExportType::Modding: return "Modding";
        default:
            throw std::runtime_error("Invalid ExportType");
    }
}

bool Companion::NodeHasChanges(const std::string& path) {

    if(this->gConfig.modding) {
        return true;
    }

    std::ifstream yaml(path);
    const std::vector<uint8_t> data = std::vector<uint8_t>(std::istreambuf_iterator( yaml ), {});
    this->gCurrentHash = CalculateHash(data);
    bool needsInit = true;

    if(this->gHashNode[path]) {
        auto entry = GetSafeNode<YAML::Node>(this->gHashNode, path);
        const auto hash = GetSafeNode<std::string>(entry, "hash");
        auto modes = GetSafeNode<YAML::Node>(entry, "extracted");
        auto extracted = GetSafeNode<bool>(modes, ExportTypeToString(this->gConfig.exporterType));

        if(hash == this->gCurrentHash) {
            needsInit = false;
            if(extracted) {
                SPDLOG_INFO("Skipping {} as it has not changed", path);
                return false;
            }
        }
    }

    if(needsInit) {
        this->gHashNode[path] = YAML::Node();
        this->gHashNode[path]["hash"] = this->gCurrentHash;
        this->gHashNode[path]["extracted"] = YAML::Node();
        for(size_t m = 0; m <= static_cast<size_t>(ExportType::Modding); m++) {
            this->gHashNode[path]["extracted"][ExportTypeToString(static_cast<ExportType>(m))] = false;
        }
    }

    return true;
}

void Companion::LoadYAMLRecursively(const std::string &dirPath, std::vector<YAML::Node> &result, bool skipRoot) {
    for (const auto &entry : std::filesystem::directory_iterator(dirPath)) {
        if (entry.is_directory()) {
            // Skip the root directory if specified
            if (skipRoot && entry.path() == dirPath) {
                continue;
            }

            // Recursive call for subdirectories
            LoadYAMLRecursively(entry.path(), result, false);
        } else if (entry.path().extension() == ".yaml" || entry.path().extension() == ".yml") {
            // Load YAML file and add it to the result vector
            result.push_back(YAML::LoadFile(entry.path().string()));
        }
    }
}

/**
 * Config yaml requires tables: [assets/courses]
 * Activate the factory using a normal asset yaml with type, input_directory, and output_directory nodes.
 */
void Companion::ProcessTables(YAML::Node& rom) {
    auto dirs = rom["metadata"].as<std::vector<std::string>>();

    for (const auto &dir : dirs) {
        std::vector<YAML::Node> configNodes;
        LoadYAMLRecursively(dir, configNodes, true);
        gCourseMetadata[dir] = configNodes;
    }

    // Write yaml data to console
    if (this->IsDebug()) {
        SPDLOG_INFO("------ Metadata ouptut ------");
        for (auto &node : gCourseMetadata[dirs[0]]) {
            std::cout << node << std::endl;
            SPDLOG_INFO("------------");
        }
        SPDLOG_INFO("------ Metadata end ------");
    }
}

void Companion::Process() {

    if(!fs::exists("config.yml")) {
        SPDLOG_ERROR("No config file found");
        return;
    }

    auto start = duration_cast<milliseconds>(system_clock::now().time_since_epoch());

    std::ifstream input( this->gRomPath, std::ios::binary );
    this->gRomData = std::vector<uint8_t>( std::istreambuf_iterator( input ), {} );
    input.close();

    this->gCartridge = std::make_shared<N64::Cartridge>(this->gRomData);
    this->gCartridge->Initialize();

    YAML::Node config = YAML::LoadFile("config.yml");

    if(!config[this->gCartridge->GetHash()]){
        SPDLOG_ERROR("No config found for {}", this->gCartridge->GetHash());
        return;
    }

    auto rom = config[this->gCartridge->GetHash()];
    auto cfg = rom["config"];

    if(!cfg) {
        SPDLOG_ERROR("No config found for {}", this->gCartridge->GetHash());
        return;
    }

    if (rom["metadata"]) {
        ProcessTables(rom);
    }

    if(rom["segments"]) {
        auto segments = rom["segments"].as<std::vector<uint32_t>>();
        for (int i = 0; i < segments.size(); i++) {
            this->gConfig.segment.global[i + 1] = segments[i];
        }
    }
    this->gAssetPath = rom["path"].as<std::string>();
    auto opath = cfg["output"];
    auto gbi = cfg["gbi"];
    auto modding_path = opath && opath["modding"] ? opath["modding"].as<std::string>() : "modding";

    this->gConfig.moddingPath = modding_path;
    switch (this->gConfig.exporterType) {
        case ExportType::Binary: {
            this->gConfig.outputPath = opath && opath["binary"] ? opath["binary"].as<std::string>() : "generic.otr";
            break;
        }
        case ExportType::Header: {
            this->gConfig.outputPath = opath && opath["headers"] ? opath["headers"].as<std::string>() : "headers";
            break;
        }
        case ExportType::Code: {
            this->gConfig.outputPath = opath && opath["code"] ? opath["code"].as<std::string>() : "code";
            break;
        }
        case ExportType::Modding: {
            this->gConfig.outputPath = modding_path;
            break;
        }
    }

    if(gbi) {
        auto key = gbi.as<std::string>();

        if(key == "F3D") {
            this->gConfig.gbi.version = GBIVersion::f3d;
        } else if(key == "F3DEX") {
            this->gConfig.gbi.version = GBIVersion::f3dex;
        } else if(key == "F3DB") {
            this->gConfig.gbi.version = GBIVersion::f3db;
        } else if(key == "F3DEX2") {
            this->gConfig.gbi.version = GBIVersion::f3dex2;
        } else if(key == "F3DEXB") {
            this->gConfig.gbi.version = GBIVersion::f3dexb;
        } else if (key == "F3DEX_MK64") {
            this->gConfig.gbi.version = GBIVersion::f3dex;
            this->gConfig.gbi.subversion = GBIMinorVersion::Mk64;
        } else {
            SPDLOG_ERROR("Invalid GBI version");
            return;
        }
    }

    if(auto sort = cfg["sort"]) {
        if(sort.IsSequence()) {
            this->gWriteOrder = sort.as<std::vector<std::string>>();
        } else {
            this->gWriteOrder = sort.as<std::string>();
        }
    } else {
        this->gWriteOrder = std::vector<std::string> {
            "LIGHTS", "TEXTURE", "VTX", "GFX"
        };
    }

    if(this->gConfig.exporterType == ExportType::Code && this->gConfig.modding) {
        this->ParseModdingConfig();
    }

    if(std::holds_alternative<std::vector<std::string>>(this->gWriteOrder)) {
        for (auto& [key, _] : this->gFactories) {
            auto entries = std::get<std::vector<std::string>>(this->gWriteOrder);

            if(std::find(entries.begin(), entries.end(), key) != entries.end()) {
                continue;
            }

            entries.push_back(key);
        }
    }

    if(cfg["enums"]) {
        auto enums = GetSafeNode<std::vector<std::string>>(cfg, "enums");
        for (auto& file : enums) {
            this->ParseEnums(file);
        }
    }

    if(cfg["logging"]){
        auto level = cfg["logging"].as<std::string>();
        if(level == "TRACE") {
            spdlog::set_level(spdlog::level::trace);
        } else if(level == "DEBUG") {
            spdlog::set_level(spdlog::level::debug);
        } else if(level == "INFO") {
            spdlog::set_level(spdlog::level::info);
        } else if(level == "WARN") {
            spdlog::set_level(spdlog::level::warn);
        } else if(level == "ERROR") {
            spdlog::set_level(spdlog::level::err);
        } else if(level == "CRITICAL") {
            spdlog::set_level(spdlog::level::critical);
        } else if(level == "OFF") {
            spdlog::set_level(spdlog::level::off);
        } else {
            throw std::runtime_error("Invalid logging level, please use TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL or OFF");
        }
    }

    this->ParseHash();

    SPDLOG_INFO("------------------------------------------------");
    spdlog::set_pattern(line);

    SPDLOG_INFO("Starting Torch...");
    SPDLOG_INFO("Game: {}", this->gCartridge->GetGameTitle());
    SPDLOG_INFO("CRC: {}", this->gCartridge->GetCRC());
    SPDLOG_INFO("Version: {}", this->gCartridge->GetVersion());
    SPDLOG_INFO("Country: [{}]", this->gCartridge->GetCountryCode());
    SPDLOG_INFO("Hash: {}", this->gCartridge->GetHash());
    SPDLOG_INFO("Assets: {}", this->gAssetPath);

    AudioManager::Instance = new AudioManager();
    auto wrapper = this->gConfig.exporterType == ExportType::Binary ? new SWrapper(this->gConfig.outputPath) : nullptr;

    auto vWriter = LUS::BinaryWriter();
    vWriter.SetEndianness(LUS::Endianness::Big);
    vWriter.Write(static_cast<uint8_t>(LUS::Endianness::Big));
    vWriter.Write(this->gCartridge->GetCRC());

    for (const auto & entry : fs::recursive_directory_iterator(this->gAssetPath)){
        if(entry.is_directory())  {
            continue;
        }

        const auto yamlPath = entry.path().string();

        if(yamlPath.find(".yaml") == std::string::npos && yamlPath.find(".yml") == std::string::npos) {
            continue;
        }

        YAML::Node root = YAML::LoadFile(yamlPath);
        this->gCurrentDirectory = relative(entry.path(), this->gAssetPath).replace_extension("");
        this->gCurrentFile = yamlPath;

        // Set compressed file offsets and compression type
        if (auto segments = root[":config"]["segments"]) {
            if (segments.IsSequence() && segments.size() > 0) {
                if (segments[0].IsSequence() && segments[0].size() == 2) {
                    gCurrentSegmentNumber = segments[0][0].as<uint32_t>();
                    gCurrentFileOffset = segments[0][1].as<uint32_t>();
                    gCurrentCompressionType = GetCompressionType(this->gRomData, gCurrentFileOffset);
                } else {
                    throw std::runtime_error("Incorrect yaml syntax for segments.\n\nThe yaml expects:\n:config:\n  segments:\n  - [<segment>, <file_offset>]\n\nLike so:\nsegments:\n  - [0x06, 0x821D10]");
                }
            }
        }

        for(auto asset = root.begin(); asset != root.end(); ++asset){
            auto node = asset->second;
            auto entryName = asset->first.as<std::string>();

            // Parse horizontal assets
            if(node["files"]){
                auto segment = node["segment"] ? node["segment"].as<uint8_t>() : -1;
                const auto files = node["files"];
                for (const auto& file : files) {
                    auto assetNode = file.as<YAML::Node>();
                    auto childName = assetNode["name"].as<std::string>();
                    auto output = (this->gCurrentDirectory / entryName / childName).string();
                    std::replace(output.begin(), output.end(), '\\', '/');

                    if(assetNode["type"]){
                        const auto type = GetSafeNode<std::string>(assetNode, "type");
                        if(type == "SAMPLE"){
                            AudioManager::Instance->bind_sample(assetNode, output);
                        }
                    }

                    if(!assetNode["offset"]) {
                        continue;
                    }

                    if(segment != -1 || gCurrentSegmentNumber) {
                        assetNode["offset"] = (segment << 24) | assetNode["offset"].as<uint32_t>();
                    }

                    this->gAddrMap[this->gCurrentFile][assetNode["offset"].as<uint32_t>()] = std::make_tuple(output, assetNode);
                }
            } else {

                auto output = (this->gCurrentDirectory / entryName).string();
                std::replace(output.begin(), output.end(), '\\', '/');


                if(node["type"]){
                    const auto type = GetSafeNode<std::string>(node, "type");
                    if(type == "SAMPLE"){
                        AudioManager::Instance->bind_sample(node, output);
                    }
                }

                if(!node["offset"])  {
                    continue;
                }

                if(gCurrentSegmentNumber) {
                    if (IS_SEGMENTED(node["offset"].as<uint32_t>()) == false) {
                        node["offset"] = (gCurrentSegmentNumber << 24) | node["offset"].as<uint32_t>();
                    }
                }

                this->gAddrMap[this->gCurrentFile][node["offset"].as<uint32_t>()] = std::make_tuple(output, node);
            }
        }

        // Stupid hack because the iteration broke the assets
        root = YAML::LoadFile(yamlPath);
        this->gConfig.segment.local.clear();
        this->gFileHeader.clear();
        this->gCurrentPad = 0;
        this->gCurrentVram = std::nullopt;
        this->gCurrentSegmentNumber = 0;
        this->gCurrentCompressionType = CompressionType::None;
        this->gTables.clear();
        this->gCurrentExternalFiles.clear();
        GFXDOverride::ClearVtx();

        if(root[":config"]) {
            this->ParseCurrentFileConfig(root[":config"]);
        }

        if(!this->NodeHasChanges(yamlPath) && !this->gNodeForceProcessing) {
            continue;
        }

        spdlog::set_pattern(regular);
        SPDLOG_INFO("------------------------------------------------");
        spdlog::set_pattern(line);

        for(auto asset = root.begin(); asset != root.end(); ++asset){

            auto entryName = asset->first.as<std::string>();
            auto assetNode = asset->second;

            if(entryName.find(":config") != std::string::npos) {
                continue;
            }

            // Parse horizontal assets
            if(assetNode["files"]){
                auto segment = assetNode["segment"] ? assetNode["segment"].as<uint8_t>() : -1;
                auto files = assetNode["files"];
                for (const auto& file : files) {
                    auto node = file.as<YAML::Node>();
                    auto childName = node["name"].as<std::string>();

                    if(!node["offset"]) {
                        continue;
                    }

                    if(segment != -1 || gCurrentFileOffset) {
                        node["offset"] = (segment << 24) | node["offset"].as<uint32_t>();
                    }

                    auto output = (this->gCurrentDirectory / entryName / childName).string();
                    std::replace(output.begin(), output.end(), '\\', '/');
                    this->gConfig.segment.temporal.clear();
                    this->ExtractNode(node, output, wrapper);
                }
            } else {
                if(gCurrentFileOffset && assetNode["offset"]) {
                    const auto offset = assetNode["offset"].as<uint32_t>();
                    if (!IS_SEGMENTED(offset)) {
                        assetNode["offset"] = (gCurrentSegmentNumber << 24) | offset;
                    }
                }
                std::string output = (this->gCurrentDirectory / entryName).string();
                std::replace(output.begin(), output.end(), '\\', '/');
                this->gConfig.segment.temporal.clear();
                this->ExtractNode(assetNode, output, wrapper);
            }

            spdlog::set_pattern(regular);
            SPDLOG_INFO("------------------------------------------------");
            spdlog::set_pattern(line);
        }

        auto fsout = fs::path(this->gConfig.outputPath);

        if(this->gConfig.exporterType == ExportType::Modding) {
            fsout /= "modding.yml";
            YAML::Node modding;

            for (const auto& [key, value] : this->gModdedAssetPaths) {
                modding["assets"][key] = value;
            }

            std::ofstream file(fsout.string(), std::ios::binary);
            file << modding;
            file.close();
        } else if(this->gConfig.exporterType != ExportType::Binary){
            std::string filename = this->gCurrentDirectory.filename().string();

            switch (this->gConfig.exporterType) {
                case ExportType::Header: {
                    fsout /= filename + ".h";
                    break;
                }
                case ExportType::Code: {
                    fsout /= this->gCurrentDirectory / (filename + ".c");
                    break;
                }
                default: break;
            }

            std::ostringstream stream;

            std::vector<WriteEntry> entries;

            if(std::holds_alternative<std::string>(this->gWriteOrder)) {
                auto sort = std::get<std::string>(this->gWriteOrder);
                for (const auto& [type, raw] : this->gWriteMap[this->gCurrentFile]) {
                    entries.insert(entries.end(), raw.begin(), raw.end());
                }

                if(sort == "OFFSET") {
                    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                        return a.addr < b.addr;
                    });
                } else if(sort == "ROFFSET") {
                    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                        return a.addr > b.addr;
                    });
                } else if(sort != "LINEAR") {
                    throw std::runtime_error("Invalid write order");
                }
            } else {
                for (const auto& type : std::get<std::vector<std::string>>(this->gWriteOrder)) {
                    entries = this->gWriteMap[this->gCurrentFile][type];

                    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                        return a.addr > b.addr;
                    });
                }
            }

            for (size_t i = 0; i < entries.size(); i++) {
                const auto result = entries[i];
                const auto hasSize = result.endptr.has_value();
                if (hasSize && this->IsDebug()) {
                    stream << "// 0x" << std::hex << std::uppercase << ASSET_PTR(result.addr) << "\n";
                }

                stream << result.buffer;

                if (hasSize && this->IsDebug()) {
                    stream << "// 0x" << std::hex << std::uppercase << ASSET_PTR(result.endptr.value()) << "\n\n";
                }

                if(hasSize && i < entries.size() - 1 && this->gConfig.exporterType == ExportType::Code){
                    int32_t startptr = ASSET_PTR(result.endptr.value());
                    int32_t end = ASSET_PTR(entries[i + 1].addr);

                    uint32_t alignment = entries[i + 1].alignment;
                    int32_t gap = end - startptr;

                    if(gap < 0) {
                        stream << "// WARNING: Overlap detected between 0x" << std::hex << startptr << " and 0x" << end << " with size 0x" << std::abs(gap) << "\n";
                        SPDLOG_WARN("Overlap detected between 0x{:X} and 0x{:X} with size 0x{:X} on file {}", startptr, end, gap, this->gCurrentFile);
                    } else if(gap < 0x10 && gap >= alignment && end % 0x10 == 0 && this->gEnablePadGen) {
                        SPDLOG_WARN("Gap detected between 0x{:X} and 0x{:X} with size 0x{:X} on file {}", startptr, end, gap, this->gCurrentFile);
                        SPDLOG_WARN("Creating pad of 0x{:X} bytes", gap);
                        const auto padfile = this->gCurrentDirectory.filename().string();
                        if(this->IsDebug()){
                            stream << "// 0x" << std::hex << std::uppercase << startptr << "\n";
                        }
                        stream << "char pad_" << padfile << "_" << std::to_string(gCurrentPad++) << "[] = {\n" << tab;
                        auto gapSize = gap & ~3;
                        for(size_t j = 0; j < gapSize; j++){
                            stream << "0x00, ";
                        }
                        stream << "\n};\n";
                        if(this->IsDebug()){
                            stream << "// 0x" << std::hex << std::uppercase << end << "\n\n";
                        } else {
                            stream << "\n";
                        }
                    } else if(gap > 0x10) {
                        stream << "// WARNING: Gap detected between 0x" << std::hex << startptr << " and 0x" << end << " with size 0x" << gap << "\n";
                    }
                }
            }

            this->gWriteMap.clear();

            std::string buffer = stream.str();

            if(buffer.empty()) {
                continue;
            }

            std::string output = fsout.string();
            std::replace(output.begin(), output.end(), '\\', '/');
            if(!exists(fs::path(output).parent_path())){
                create_directories(fs::path(output).parent_path());
            }

            std::ofstream file(output, std::ios::binary);

            if(this->gConfig.exporterType == ExportType::Header) {
                std::string symbol = entry.path().stem().string();
                std::transform(symbol.begin(), symbol.end(), symbol.begin(), toupper);
                file << "#ifndef " << symbol << "_H" << std::endl;
                file << "#define " << symbol << "_H" << std::endl << std::endl;
                if(!this->gFileHeader.empty()) {
                    file << this->gFileHeader << std::endl;
                }
                file << buffer;
                file << std::endl << "#endif" << std::endl;
            } else {
                if(!this->gFileHeader.empty()) {
                    file << this->gFileHeader << std::endl;
                }
                file << buffer;
            }

            file.close();
        }

        this->gHashNode[this->gCurrentFile]["extracted"][ExportTypeToString(this->gConfig.exporterType)] = true;
    }

    if(wrapper != nullptr) {
        SPDLOG_INFO("Writing version file");
        wrapper->CreateFile("version", vWriter.ToVector());
        vWriter.Close();
        wrapper->Close();
    }

    // Write entries hash
    std::ofstream file("torch.hash.yml", std::ios::binary);
    file << this->gHashNode;
    file.close();

    auto end = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    auto level = spdlog::get_level();
    spdlog::set_level(spdlog::level::info);
    SPDLOG_INFO("Done! Took {}ms", end.count() - start.count());
    spdlog::set_level(level);
    spdlog::set_pattern(regular);
    SPDLOG_INFO("------------------------------------------------");

    Decompressor::ClearCache();
    this->gCartridge = nullptr;
    Instance = nullptr;
}

void Companion::Pack(const std::string& folder, const std::string& output) {

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    SPDLOG_INFO("------------------------------------------------");

    SPDLOG_INFO("Starting Torch...");
    SPDLOG_INFO("Scanning {}", folder);

    auto start = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    std::unordered_map<std::string, std::vector<char>> files;

    for (const auto & entry : fs::recursive_directory_iterator(folder)){
        if(entry.is_directory())  {
            continue;
        }

        std::ifstream input( entry.path(), std::ios::binary );
        auto data = std::vector( std::istreambuf_iterator( input ), {} );
        input.close();
        files[entry.path().string()] = data;
    }

    auto wrapper = SWrapper(output);

    for(auto& [path, data] : files){
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        // Remove parent folder
        normalized = normalized.substr(folder.length() + 1);
        wrapper.CreateFile(normalized, data);
        SPDLOG_INFO("> Added {}", normalized);
    }

    auto end = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    SPDLOG_INFO("Done! Took {}ms", end.count() - start.count());
    SPDLOG_INFO("Exported to {}", output);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    SPDLOG_INFO("------------------------------------------------");

    wrapper.Close();
}

std::optional<std::tuple<std::string, YAML::Node>> Companion::RegisterAsset(const std::string& name, YAML::Node& node) {
    if(!node["offset"]) {
        return std::nullopt;
    }

    this->gAssetDependencies[this->gCurrentFile][name] = std::make_pair(node, false);

    auto output = (this->gCurrentDirectory / name).string();
    std::replace(output.begin(), output.end(), '\\', '/');

    auto entry = std::make_tuple(output, node);
    this->gAddrMap[this->gCurrentFile][node["offset"].as<uint32_t>()] = entry;



    return entry;
}

void Companion::RegisterFactory(const std::string& type, const std::shared_ptr<BaseFactory>& factory) {
    this->gFactories[type] = factory;
    SPDLOG_DEBUG("Registered factory for {}", type);
}

std::optional<std::shared_ptr<BaseFactory>> Companion::GetFactory(const std::string &type) {
    if(!this->gFactories.contains(type)){
        return std::nullopt;
    }

    return this->gFactories[type];
}

/**
 * @param offset Rom offset of compressed mio0 file.
 * @returns CompressionType
 */
CompressionType Companion::GetCompressionType(std::vector<uint8_t>& buffer, const uint32_t offset) {
    if (offset) {
        LUS::BinaryReader reader((char*) buffer.data() + offset, sizeof(uint32_t));
        reader.SetEndianness(LUS::Endianness::Big);

        const std::string header = reader.ReadCString();

        // Check if a compressed header exists
        if (header == "MIO0") {
            return CompressionType::MIO0;
        } else if (header == "YAY0") {
            return CompressionType::YAY0;
        } else if (header == "YAZ0") {
            return CompressionType::YAZ0;
        }
    }
    return CompressionType::None;
}

std::optional<Table> Companion::SearchTable(uint32_t addr){
    for(auto& table : this->gTables){
        if(addr >= table.start && addr <= table.end){
            return table;
        }
    }

    return std::nullopt;
}

std::optional<std::string> Companion::GetEnumFromValue(const std::string& key, int32_t id) {
    if(!this->gEnums.contains(key)){
        return std::nullopt;
    }

    if(!this->gEnums[key].contains(id)){
        return std::nullopt;
    }

    return this->gEnums[key][id];
}

std::optional<std::uint32_t> Companion::GetFileOffsetFromSegmentedAddr(const uint8_t segment) const {

    auto segments = this->gConfig.segment;

    if(segments.temporal.contains(segment)) {
        return segments.temporal[segment];
    }

    if(segments.local.contains(segment)) {
        return segments.local[segment];
    }

    if(segments.global.contains(segment)) {
        return segments.global[segment];
    }

    return std::nullopt;
}

std::optional<std::tuple<std::string, YAML::Node>> Companion::GetNodeByAddr(const uint32_t addr){
    if(!this->gAddrMap.contains(this->gCurrentFile)){
        return std::nullopt;
    }

    if(!this->gAddrMap[this->gCurrentFile].contains(addr)){
        for (auto &file : this->gCurrentExternalFiles) {
            if (!this->gAddrMap.contains(file)) {
                SPDLOG_INFO("External File {} Not Found.", file);
                continue;
            }

            if (!this->gAddrMap[file].contains(addr)) {
                continue;
            }
            return this->gAddrMap[file][addr];
        }
        return std::nullopt;
    }

    return this->gAddrMap[this->gCurrentFile][addr];
}

std::optional<std::vector<std::tuple<std::string, YAML::Node>>> Companion::GetNodesByType(const std::string& type){
    std::vector<std::tuple<std::string, YAML::Node>> nodes;

    if(!this->gAddrMap.contains(this->gCurrentFile)){
        return nodes;
    }

    for(auto& [addr, tpl] : this->gAddrMap[this->gCurrentFile]){
        auto [name, node] = tpl;
        const auto n_type = GetSafeNode<std::string>(node, "type");
        if(node["autogen"]){
            SPDLOG_DEBUG("Skipping autogenerated asset {}", name);
            continue;
        }
        if(n_type == type){
            nodes.push_back(tpl);
        }
    }

    return nodes;

}

std::string Companion::NormalizeAsset(const std::string& name) const {
    auto path = fs::path(this->gCurrentFile).stem().string() + "_" + name;
    return path;
}

std::string Companion::CalculateHash(const std::vector<uint8_t>& data) {
    return Chocobo1::SHA1().addData(data).finalize().toString();
}

static std::string ConvertType(std::string type) {
    int index;

    if((index = type.find(':')) != std::string::npos) {
        type = type.substr(index + 1);
    }
    type = std::regex_replace(type, std::regex(R"([^_A-Za-z0-9]*)"), "");
    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
    return type;
}

std::optional<YAML::Node> Companion::AddAsset(YAML::Node asset) {
    if(!asset["offset"] || !asset["type"]) {
        return std::nullopt;
    }
    const auto type = GetSafeNode<std::string>(asset, "type");
    const auto offset = GetSafeNode<uint32_t>(asset, "offset");
    const auto symbol = GetSafeNode<std::string>(asset, "symbol", "");
    const auto decl = this->GetNodeByAddr(offset);


    if(decl.has_value()) {
        return std::get<1>(decl.value());
    }

    auto rom = this->GetRomData();
    auto factory = this->GetFactory(type);

    if(!factory.has_value()) {
        return std::nullopt;
    }

    std::string output;
    std::string typeId = ConvertType(type);

    if(symbol != "") {
        output = symbol;
    } else if(Decompressor::IsSegmented(offset)){
        output = this->NormalizeAsset("seg" + std::to_string(SEGMENT_NUMBER(offset)) +"_" + typeId + "_" + Torch::to_hex(SEGMENT_OFFSET(offset), false));
    } else {
        output = this->NormalizeAsset(typeId + "_" + Torch::to_hex(offset, false));
    }
    asset["autogen"] = true;
    asset["symbol"] = output;

    auto result = this->RegisterAsset(output, asset);

    if(result.has_value()){
        asset["path"] = std::get<0>(result.value());

        return std::get<1>(result.value());
    }

    return std::nullopt;
}

void Companion::AddTlutTextureMap(std::string index, std::shared_ptr<TextureData> entry) {
    this->TlutTextureMap[index] = entry;
}