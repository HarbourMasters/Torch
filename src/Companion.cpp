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

namespace fs = std::filesystem;
using namespace std::chrono;

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

void Companion::Process() {

    const std::string regular = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";
    const std::string line    = "[%Y-%m-%d %H:%M:%S.%e] [%l] > %v";

    auto start = duration_cast<milliseconds>(system_clock::now().time_since_epoch());

    std::ifstream input( this->gRomPath, std::ios::binary );
    this->gRomData = std::vector<uint8_t>( std::istreambuf_iterator<char>( input ), {} );
    input.close();

    this->cartridge = new N64::Cartridge(this->gRomData);
    cartridge->Initialize();

    YAML::Node config = YAML::LoadFile("config.yml");

    if(!config[cartridge->GetHash()]){
        SPDLOG_ERROR("No config found for {}", cartridge->GetHash());
        return;
    }

    auto cfg = config[cartridge->GetHash()];
    auto path = cfg["path"].as<std::string>();
    auto otr = cfg["output"].as<std::string>();

    SPDLOG_INFO("------------------------------------------------");
    spdlog::set_pattern(line);

    SPDLOG_INFO("Starting CubeOS...");
    SPDLOG_INFO("Game: {}", cartridge->GetGameTitle());
    SPDLOG_INFO("CRC: {}", cartridge->GetCRC());
    SPDLOG_INFO("Version: {}", cartridge->GetVersion());
    SPDLOG_INFO("Country: [{}]", cartridge->GetCountryCode());
    SPDLOG_INFO("Hash: {}", cartridge->GetHash());
    SPDLOG_INFO("Assets: {}", path);

    auto wrapper = SWrapper(otr);

    auto vWriter = LUS::BinaryWriter();
    vWriter.SetEndianness(LUS::Endianness::Big);
    vWriter.Write((uint8_t) LUS::Endianness::Big);
    vWriter.Write(cartridge->GetCRC());

    for (const auto & entry : fs::recursive_directory_iterator(path)){
        if(entry.is_directory()) continue;
        YAML::Node root = YAML::LoadFile(entry.path().string());
        auto directory = relative(entry.path(), path).replace_extension("");
        this->gCurrentFile = entry.path().string();
        std::ostringstream headerStr;

        for(auto asset = root.begin(); asset != root.end(); asset++){
            auto node = asset->second;
            if(!asset->second["offset"]) continue;

            auto output = (directory / asset->first.as<std::string>()).string();
            std::replace(output.begin(), output.end(), '\\', '/');

            this->gAddrMap[this->gCurrentFile][node["offset"].as<uint32_t>()] = std::make_tuple(output, node);
        }

        // Stupid hack because the iteration broke the assets
        root = YAML::LoadFile(entry.path().string());

        for(auto asset = root.begin(); asset != root.end(); asset++){
            auto type = asset->second["type"].as<std::string>();
            std::transform(type.begin(), type.end(), type.begin(), ::toupper);

            auto entryName = asset->first.as<std::string>();
            auto factory = this->GetFactory(type);
            if(!factory.has_value()){
                SPDLOG_ERROR("No factory found for {}", asset->first.as<std::string>());
                continue;
            }

            spdlog::set_pattern(regular);
            SPDLOG_INFO("------------------------------------------------");
            spdlog::set_pattern(line);
            SPDLOG_INFO("Processing {} [{}]", asset->first.as<std::string>(), type);
            SPDLOG_INFO("Root: {}", entry.path().string());

            auto result = factory->get()->parse(this->gRomData, asset->second);
            if(!result.has_value()){
                SPDLOG_ERROR("Failed to process {}", asset->first.as<std::string>());
                continue;
            }

            auto exporter = factory->get()->GetExporter(gExporterType);
            if(!exporter.has_value()){
                SPDLOG_ERROR("No exporter found for {}", asset->first.as<std::string>());
                continue;
            }

            std::string output;
            std::ostringstream stream;

            switch (gExporterType) {
                case ExportType::Header: {
                    output = (directory / entryName).string();
                    exporter->get()->Export(stream, result.value(), entryName, asset->second, &output);
                    headerStr << stream.str() << "\n";
                    break;
                }
                case ExportType::Code: {
                    output = (directory / entryName).string();
                    std::replace(output.begin(), output.end(), '\\', '/');
                    std::string dpath = "code/" + output;

                    if(!fs::exists(fs::path(dpath).parent_path())){
                        fs::create_directories(fs::path(dpath).parent_path());
                    }

                    exporter->get()->Export(stream, result.value(), entryName, asset->second, &output);
                    std::ofstream file(dpath + ".inc.c", std::ios::binary);
                    file << stream.str();
                    file.close();
                    break;
                }
                case ExportType::Binary: {
                    output = (directory / entryName).string();
                    std::replace(output.begin(), output.end(), '\\', '/');

                    exporter->get()->Export(stream, result.value(), entryName, asset->second, &output);
                    auto data = stream.str();
                    wrapper.CreateFile(output, std::vector<char>(data.begin(), data.end()));
                    break;
                }
            }

            SPDLOG_INFO("Processed {}", output);
        }

        if(gExporterType == ExportType::Header){
            std::string output = relative(entry.path(), path).replace_extension(".h").string();
            std::replace(output.begin(), output.end(), '\\', '/');
            std::string dpath = "headers/" + output;

            if(!fs::exists(fs::path(dpath).parent_path())){
                fs::create_directories(fs::path(dpath).parent_path());
            }

            std::ofstream file(dpath, std::ios::binary);
            file << headerStr.str();
            file.close();
        }
    }

    spdlog::set_pattern(regular);
    SPDLOG_INFO("------------------------------------------------");
    spdlog::set_pattern(line);
    SPDLOG_INFO("Writing version file");
    wrapper.CreateFile("version", vWriter.ToVector());
    vWriter.Close();
    auto end = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    SPDLOG_INFO("Done! Took {}ms", end.count() - start.count());
    SPDLOG_INFO("Exported to {}", otr);
    spdlog::set_pattern(regular);
    SPDLOG_INFO("------------------------------------------------");

    MIO0Decoder::ClearCache();
    wrapper.Close();
    this->cartridge = nullptr;
    Companion::Instance = nullptr;
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
            auto data = std::vector<char>( std::istreambuf_iterator<char>( input ), {} );
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