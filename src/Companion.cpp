#include "Companion.h"

#include "storm/SWrapper.h"
#include "utils/Decompressor.h"
#include "factories/sm64/AnimationFactory.h"
#include "factories/sm64/DialogFactory.h"
#include "factories/sm64/DictionaryFactory.h"
#include "factories/sm64/TextFactory.h"
#include "factories/sm64/GeoLayoutFactory.h"
#include "factories/BankFactory.h"
#include "factories/AudioHeaderFactory.h"
#include "factories/SampleFactory.h"
#include "factories/SequenceFactory.h"
#include "factories/VtxFactory.h"
#include "factories/TextureFactory.h"
#include "factories/DisplayListFactory.h"
#include "factories/BlobFactory.h"
#include "factories/LightsFactory.h"
#include "factories/mk64/WaypointFactory.h"
#include "spdlog/spdlog.h"
#include "hj/sha1.h"

#include <fstream>
#include <iostream>
#include <filesystem>

using namespace std::chrono;
namespace fs = std::filesystem;

static const std::string regular = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";
static const std::string line    = "[%Y-%m-%d %H:%M:%S.%e] [%l] > %v";

void Companion::Init(const ExportType type) {

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    this->gConfig.exporterType = type;
    this->RegisterFactory("BLOB", std::make_shared<BlobFactory>());
    this->RegisterFactory("TEXTURE", std::make_shared<TextureFactory>());
    this->RegisterFactory("VTX", std::make_shared<VtxFactory>());
    this->RegisterFactory("LIGHTS", std::make_shared<LightsFactory>());
    this->RegisterFactory("GFX", std::make_shared<DListFactory>());
    this->RegisterFactory("AUDIO:HEADER", std::make_shared<AudioHeaderFactory>());
    this->RegisterFactory("SEQUENCE", std::make_shared<SequenceFactory>());
    this->RegisterFactory("SAMPLE", std::make_shared<SampleFactory>());
    this->RegisterFactory("BANK", std::make_shared<BankFactory>());

    // SM64 specific
    this->RegisterFactory("SM64:DIALOG", std::make_shared<SM64::DialogFactory>());
    this->RegisterFactory("SM64:TEXT", std::make_shared<SM64::TextFactory>());
    this->RegisterFactory("SM64:DICTIONARY", std::make_shared<SM64::DictionaryFactory>());
    this->RegisterFactory("SM64:ANIM", std::make_shared<SM64::AnimationFactory>());
    this->RegisterFactory("SM64:GEO_LAYOUT", std::make_shared<SM64::GeoLayoutFactory>());

    // MK64 specific
    this->RegisterFactory("MK64:TRACKWAYPOINTS", std::make_shared<MK64::WaypointFactory>());

    this->Process();
}

void Companion::ExtractNode(YAML::Node& node, std::string& name, SWrapper* binary) {
    std::ostringstream stream;

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
        SPDLOG_ERROR("No factory found for {}", name);
        return;
    }

    auto impl = factory->get();

    std::optional<std::shared_ptr<IParsedData>> result;
    if(this->gConfig.modding) {
        if(impl->SupportModdedAssets() && this->gModdedAssetPaths.contains(name)) {

            auto path = fs::path(this->gConfig.moddingPath) / this->gModdedAssetPaths[name];
            if(!fs::exists(path)) {
                SPDLOG_ERROR("Modded asset {} not found", this->gModdedAssetPaths[name]);
                return;
            }

            std::ifstream input(path, std::ios::binary);
            std::vector<uint8_t> data = std::vector<uint8_t>( std::istreambuf_iterator( input ), {});
            input.close();

            result = factory->get()->parse_modding(data, node);
        } else {
            result = factory->get()->parse(this->gRomData, node);
        }
    } else {
        result = factory->get()->parse(this->gRomData, node);
    }
    if(!result.has_value()){
        SPDLOG_ERROR("Failed to process {}", name);
        return;
    }

    auto exporter = factory->get()->GetExporter(this->gConfig.exporterType);
    if(!exporter.has_value()){
        SPDLOG_ERROR("No exporter found for {}", name);
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
            exporter->get()->Export(stream, result.value(), name, node, &name);

            if(this->gConfig.exporterType == ExportType::Code) {
                if(node["pad"]){
                    auto filename = this->gCurrentDirectory.filename().string();
                    auto pad = GetSafeNode<uint32_t>(node, "pad");
                    stream << "char pad_" << filename << "_" << std::to_string(gCurrentPad++) << "[] = {\n" << tab;
                    for(int i = 0; i < pad; i++){
                        stream << "0x00, ";
                    }
                    stream << "\n};\n\n";
                }
            }

            break;
        }
    }

    SPDLOG_INFO("Processed {}", name);
    if(node["offset"]) {
        this->gWriteMap[this->gCurrentFile][type].emplace_back(node["offset"].as<uint32_t>(), stream.str());
    }
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

    if(rom["segments"]) {
        auto segments = rom["segments"].as<std::vector<uint32_t>>();
        for (int i = 0; i < segments.size(); i++) {
            this->gConfig.segment.global[i + 1] = segments[i];
        }
    }
    auto path = rom["path"].as<std::string>();
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

    SPDLOG_INFO("------------------------------------------------");
    spdlog::set_pattern(line);

    SPDLOG_INFO("Starting Torch...");
    SPDLOG_INFO("Game: {}", this->gCartridge->GetGameTitle());
    SPDLOG_INFO("CRC: {}", this->gCartridge->GetCRC());
    SPDLOG_INFO("Version: {}", this->gCartridge->GetVersion());
    SPDLOG_INFO("Country: [{}]", this->gCartridge->GetCountryCode());
    SPDLOG_INFO("Hash: {}", this->gCartridge->GetHash());
    SPDLOG_INFO("Assets: {}", path);

    AudioManager::Instance = new AudioManager();
    auto wrapper = this->gConfig.exporterType == ExportType::Binary ? new SWrapper(this->gConfig.outputPath) : nullptr;

    auto vWriter = LUS::BinaryWriter();
    vWriter.SetEndianness(LUS::Endianness::Big);
    vWriter.Write(static_cast<uint8_t>(LUS::Endianness::Big));
    vWriter.Write(this->gCartridge->GetCRC());

    for (const auto & entry : fs::recursive_directory_iterator(path)){
        if(entry.is_directory())  {
            continue;
        }

        const auto yamlPath = entry.path().string();

        if(yamlPath.find(".yaml") == std::string::npos && yamlPath.find(".yml") == std::string::npos) {
            continue;
        }

        YAML::Node root = YAML::LoadFile(yamlPath);
        this->gCurrentDirectory = relative(entry.path(), path).replace_extension("");
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

        if(root[":config"]) {
            this->ParseCurrentFileConfig(root[":config"]);
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
                if(gCurrentFileOffset) {
                    if (IS_SEGMENTED(assetNode["offset"].as<uint32_t>()) == false) {
                        assetNode["offset"] = (gCurrentSegmentNumber << 24) | assetNode["offset"].as<uint32_t>();
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

            if(std::holds_alternative<std::string>(this->gWriteOrder)) {
                auto sort = std::get<std::string>(this->gWriteOrder);
                std::vector<std::pair<uint32_t, std::string>> outbuf;
                for (const auto& [type, buffer] : this->gWriteMap[this->gCurrentFile]) {
                    outbuf.insert(outbuf.end(), buffer.begin(), buffer.end());
                }
                this->gWriteMap.clear();

                if(sort == "OFFSET") {
                    std::sort(outbuf.begin(), outbuf.end(), [](const auto& a, const auto& b) {
                        return std::get<uint32_t>(a) < std::get<uint32_t>(b);
                    });
                } else if(sort == "ROFFSET") {
                    std::sort(outbuf.begin(), outbuf.end(), [](const auto& a, const auto& b) {
                        return std::get<uint32_t>(a) > std::get<uint32_t>(b);
                    });
                } else if(sort != "LINEAR") {
                    throw std::runtime_error("Invalid write order");
                }

                for (auto& [symbol, buffer] : outbuf) {
                    stream << buffer;
                }
                outbuf.clear();
            } else {
                for (const auto& type : std::get<std::vector<std::string>>(this->gWriteOrder)) {
                    std::vector<std::pair<uint32_t, std::string>> outbuf = this->gWriteMap[this->gCurrentFile][type];

                    std::sort(outbuf.begin(), outbuf.end(), [](const auto& a, const auto& b) {
                        return std::get<uint32_t>(a) > std::get<uint32_t>(b);
                    });

                    for (auto& [symbol, buffer] : outbuf) {
                        stream << buffer;
                    }
                }
            }

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
    }

    if(wrapper != nullptr) {
        SPDLOG_INFO("Writing version file");
        wrapper->CreateFile("version", vWriter.ToVector());
        vWriter.Close();
        wrapper->Close();
    }
    auto end = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    SPDLOG_INFO("Done! Took {}ms", end.count() - start.count());
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
    if(!node["offset"]) return std::nullopt;

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
        return std::nullopt;
    }

    return this->gAddrMap[this->gCurrentFile][addr];
}

std::string Companion::NormalizeAsset(const std::string& name) const {
    auto path = fs::path(this->gCurrentFile).stem().string() + "_" + name;
    return path;
}

std::string Companion::CalculateHash(const std::vector<uint8_t>& data) {
    return Chocobo1::SHA1().addData(data).finalize().toString();
}