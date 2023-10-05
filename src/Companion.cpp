#include "Companion.h"

#include "storm/SWrapper.h"
#include "utils/MIODecoder.h"
#include "factories/RawFactory.h"
#include "factories/BlobFactory.h"
#include "factories/debug/MIO0Factory.h"
#include "factories/AudioHeaderFactory.h"
#include "factories/SequenceFactory.h"
#include "factories/SampleFactory.h"
#include "factories/BankFactory.h"
#include "factories/TextureFactory.h"
#include "factories/sm64/SAnimFactory.h"
#include "factories/sm64/STextFactory.h"
#include "factories/sm64/SDialogFactory.h"
#include "factories/sm64/SDictionaryFactory.h"
#include "spdlog/spdlog.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace std::chrono;

void Companion::Init() {

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    this->RegisterFactory("AUDIO:HEADER", new AudioHeaderFactory());
    this->RegisterFactory("SEQUENCE", new SequenceFactory());
    this->RegisterFactory("SAMPLE", new SampleFactory());
    this->RegisterFactory("BANK", new BankFactory());

    this->RegisterFactory("TEXTURE", new TextureFactory());
    this->RegisterFactory("BLOB", new BlobFactory());

    // SM64 Specific
    this->RegisterFactory("SM64:DIALOG", new SDialogFactory());
    this->RegisterFactory("SM64:ANIM", new SAnimFactory());
    this->RegisterFactory("SM64:TEXT", new STextFactory());
    this->RegisterFactory("SM64:DICTIONARY", new SDictionaryFactory());

    // Debug
    this->RegisterFactory("MIO0", new MIO0Factory());

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

    for (const auto & entry : fs::directory_iterator(path)){
        YAML::Node root = YAML::LoadFile(entry.path().string());
        for(auto asset = root.begin(); asset != root.end(); ++asset){
            auto type = asset->second["type"].as<std::string>();

            LUS::BinaryWriter write = LUS::BinaryWriter();
            RawFactory* factory = this->GetFactory(type);
            factory->SetCartridge(cartridge);

            spdlog::set_pattern(regular);
            SPDLOG_INFO("------------------------------------------------");
            spdlog::set_pattern(line);
            SPDLOG_INFO("Processing {} [{}]", asset->first.as<std::string>(), type);
            SPDLOG_INFO("Root: {}", entry.path().string());

            if(!factory->process(&write, asset->second, this->gRomData)){
                SPDLOG_ERROR("Failed to process {}", asset->first.as<std::string>());
                continue;
            }

            auto buffer = write.ToVector();
            auto output = (entry.path().stem() / asset->first.as<std::string>()).string();

            std::replace(output.begin(), output.end(), '\\', '/');

            wrapper.CreateFile(output, buffer);

            if(type != "TEXTURE") {
                SPDLOG_DEBUG("Writing debug file");
                std::string dpath = "debug/" + output;
                if(!fs::exists(fs::path(dpath).parent_path())){
                    fs::create_directories(fs::path(dpath).parent_path());
                }
                std::ofstream stream(dpath, std::ios::binary);
                stream.write((char*)buffer.data(), buffer.size());
                stream.close();
            }

            SPDLOG_INFO("Processed {}", output);

            write.Close();
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

void Companion::RegisterFactory(const std::string &extension, RawFactory *factory) {
    this->gFactories[extension] = factory;
    SPDLOG_DEBUG("Registered factory for {}", extension);
}

RawFactory *Companion::GetFactory(const std::string &extension) {
    if(!this->gFactories.contains(extension)){
        throw std::runtime_error("No factory found for " + extension);
    }

    return this->gFactories[extension];
}
