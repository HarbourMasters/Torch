#include "Companion.h"

#include "storm/SWrapper.h"
#include "utils/MIODecoder.h"
#include "factories/sm64/AnimationFactory.h"
#include "factories/sm64/DialogFactory.h"
#include "factories/sm64/DictionaryFactory.h"
#include "factories/sm64/TextFactory.h"
#include "factories/BankFactory.h"
#include "factories/AudioHeaderFactory.h"
#include "factories/SampleFactory.h"
#include "factories/SequenceFactory.h"
#include "factories/VtxFactory.h"
#include "factories/TextureFactory.h"
#include "factories/DisplayListFactory.h"
#include "factories/BlobFactory.h"
#include "factories/LightsFactory.h"
#include "spdlog/spdlog.h"

#include <fstream>
#include <iostream>
#include <filesystem>

using namespace std::chrono;
namespace fs = std::filesystem;

static const std::string regular = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";
static const std::string line    = "[%Y-%m-%d %H:%M:%S.%e] [%l] > %v";

void Companion::Init(ExportType type) {

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    this->gExporterType = type;
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
    // this->RegisterFactory("GEO_LAYOUT", new SGeoFactory());

    this->Process();
}

void Companion::ExtractNode(std::ostringstream& stream, YAML::Node& node, std::string& name, SWrapper* binary) {
    auto type = node["type"].as<std::string>();
    std::transform(type.begin(), type.end(), type.begin(), ::toupper);

    SPDLOG_INFO("Processing {} [{}]", name, type);
    auto factory = this->GetFactory(type);
    if(!factory.has_value()){
        SPDLOG_ERROR("No factory found for {}", name);
        return;
    }

    auto result = factory->get()->parse(this->gRomData, node);
    if(!result.has_value()){
        SPDLOG_ERROR("Failed to process {}", name);
        return;
    }

    auto exporter = factory->get()->GetExporter(this->gExporterType);
    if(!exporter.has_value()){
        SPDLOG_ERROR("No exporter found for {}", name);
        return;
    }

    for (auto [fst, snd] : this->gAssetDependencies[this->gCurrentFile]) {
        if(snd.second) continue;
        std::string doutput = (this->gCurrentDirectory / fst).string();
        std::ranges::replace(doutput, '\\', '/');
        this->gAssetDependencies[this->gCurrentFile][fst].second = true;
        this->ExtractNode(stream, snd.first, doutput, binary);
    }

    switch (this->gExporterType) {
        case ExportType::Binary: {
            if(binary == nullptr) break;

            exporter->get()->Export(stream, result.value(), name, node, &name);
            auto data = stream.str();
            binary->CreateFile(name, std::vector(data.begin(), data.end()));
            break;
        }
        case ExportType::Code: {
            if(type == "TEXTURE") {
                factory->get()->GetExporter(ExportType::Header)->get()->Export(stream, result.value(), name, node, &name);

                std::string dpath = "code/" + name;
                if(!fs::exists(fs::path(dpath).parent_path())){
                    fs::create_directories(fs::path(dpath).parent_path());
                }

                std::ostringstream cstream;
                exporter->get()->Export(cstream, result.value(), name, node, &name);
                std::ofstream file(dpath + ".inc.c", std::ios::binary);
                file << cstream.str();
                file.close();
            } else {
                exporter->get()->Export(stream, result.value(), name, node, &name);
            }
            break;
        }
        case ExportType::Header: {
            exporter->get()->Export(stream, result.value(), name, node, &name);
            break;
        }
    }

    SPDLOG_INFO("Processed {}", name);
}

void Companion::Process() {

    auto start = duration_cast<milliseconds>(system_clock::now().time_since_epoch());

    std::ifstream input( this->gRomPath, std::ios::binary );
    this->gRomData = std::vector<uint8_t>( std::istreambuf_iterator<char>( input ), {} );
    input.close();

    this->gCartridge = new N64::Cartridge(this->gRomData);
    gCartridge->Initialize();

    YAML::Node config = YAML::LoadFile("config.yml");

    if(!config[gCartridge->GetHash()]){
        SPDLOG_ERROR("No config found for {}", gCartridge->GetHash());
        return;
    }

    auto cfg = config[gCartridge->GetHash()];
    auto path = cfg["path"].as<std::string>();
    auto otr = cfg["output"].as<std::string>();
    if(cfg["segments"]) {
        this->gSegments = cfg["segments"].as<std::vector<uint32_t>>();
    }

    SPDLOG_INFO("------------------------------------------------");
    spdlog::set_pattern(line);

    SPDLOG_INFO("Starting CubeOS...");
    SPDLOG_INFO("Game: {}", gCartridge->GetGameTitle());
    SPDLOG_INFO("CRC: {}", gCartridge->GetCRC());
    SPDLOG_INFO("Version: {}", gCartridge->GetVersion());
    SPDLOG_INFO("Country: [{}]", gCartridge->GetCountryCode());
    SPDLOG_INFO("Hash: {}", gCartridge->GetHash());
    SPDLOG_INFO("Assets: {}", path);

    auto wrapper = this->gExporterType == ExportType::Binary ? new SWrapper(otr) : nullptr;

    auto vWriter = LUS::BinaryWriter();
    vWriter.SetEndianness(LUS::Endianness::Big);
    vWriter.Write((uint8_t) LUS::Endianness::Big);
    vWriter.Write(gCartridge->GetCRC());

    for (const auto & entry : fs::recursive_directory_iterator(path)){
        if(entry.is_directory()) continue;
        YAML::Node root = YAML::LoadFile(entry.path().string());
        this->gCurrentDirectory = relative(entry.path(), path).replace_extension("");
        this->gCurrentFile = entry.path().string();
        std::ostringstream stream;

        for(auto asset = root.begin(); asset != root.end(); asset++){
            auto node = asset->second;
            if(!asset->second["offset"]) continue;

            auto output = (this->gCurrentDirectory / asset->first.as<std::string>()).string();
            std::replace(output.begin(), output.end(), '\\', '/');

            this->gAddrMap[this->gCurrentFile][node["offset"].as<uint32_t>()] = std::make_tuple(output, node);
        }

        // Stupid hack because the iteration broke the assets
        root = YAML::LoadFile(entry.path().string());

        for(auto asset = root.begin(); asset != root.end(); asset++){

            spdlog::set_pattern(regular);
            SPDLOG_INFO("------------------------------------------------");
            spdlog::set_pattern(line);

            auto entryName = asset->first.as<std::string>();
            std::string output = (this->gCurrentDirectory / entryName).string();
            std::ranges::replace(output, '\\', '/');

            this->ExtractNode(stream, asset->second, output, wrapper);
        }

        if(gExporterType != ExportType::Binary){
            std::string output = this->gCurrentDirectory.string() + "/root.inc.c";
            std::ranges::replace(output, '\\', '/');
            std::string dpath = "code/" + output;

            if(!fs::exists(fs::path(dpath).parent_path())){
                fs::create_directories(fs::path(dpath).parent_path());
            }

            std::ofstream file(dpath, std::ios::binary);
            file << stream.str();
            file.close();
        }
    }

    spdlog::set_pattern(regular);
    SPDLOG_INFO("------------------------------------------------");
    spdlog::set_pattern(line);
    if(wrapper != nullptr) {
        SPDLOG_INFO("Writing version file");
        wrapper->CreateFile("version", vWriter.ToVector());
        vWriter.Close();
        wrapper->Close();
    }
    auto end = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    SPDLOG_INFO("Done! Took {}ms", end.count() - start.count());
    SPDLOG_INFO("Exported to {}", otr);
    spdlog::set_pattern(regular);
    SPDLOG_INFO("------------------------------------------------");

    MIO0Decoder::ClearCache();
    this->gCartridge = nullptr;
    Instance = nullptr;
}

void Companion::RegisterAsset(const std::string& name, YAML::Node& node) {
    if(!node["offset"]) return;
    this->gAssetDependencies[this->gCurrentFile][name] = std::make_pair(node, false);

    auto output = (this->gCurrentDirectory / name).string();
    std::replace(output.begin(), output.end(), '\\', '/');
    this->gAddrMap[this->gCurrentFile][node["offset"].as<uint32_t>()] = std::make_tuple(output, node);
}

void Companion::RegisterFactory(const std::string& type, std::shared_ptr<BaseFactory> factory) {
    this->gFactories[type] = factory;
    SPDLOG_DEBUG("Registered factory for {}", type);
}

std::optional<std::shared_ptr<BaseFactory>> Companion::GetFactory(const std::string &extension) {
    if(!this->gFactories.contains(extension)){
        return std::nullopt;
    }

    return this->gFactories[extension];
}

std::optional<std::uint32_t> Companion::GetSegmentedAddr(uint8_t segment) {
    if(segment >= this->gSegments.size()) {
        return std::nullopt;
    }

    return this->gSegments[segment];
}

std::optional<std::tuple<std::string, YAML::Node>> Companion::GetNodeByAddr(uint32_t addr){
    if(!this->gAddrMap.contains(this->gCurrentFile)){
        return std::nullopt;
    }

    if(!this->gAddrMap[this->gCurrentFile].contains(addr)){
        return std::nullopt;
    }

    return this->gAddrMap[this->gCurrentFile][addr];
}

void Companion::Pack(const std::string& folder, const std::string& output) {

        spdlog::set_level(spdlog::level::debug);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

        SPDLOG_INFO("------------------------------------------------");

        SPDLOG_INFO("Starting CubeOS...");
        SPDLOG_INFO("Scanning {}", folder);

        auto start = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
        std::unordered_map<std::string, std::vector<char>> files;

        for (const auto & entry : fs::recursive_directory_iterator(folder)){
            if(entry.is_directory()) continue;
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