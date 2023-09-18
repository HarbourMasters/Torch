#include "Companion.h"

#include "storm/SWrapper.h"
#include "utils/MIODecoder.h"
#include "audio/AudioManager.h"
#include "factories/RawFactory.h"
#include "factories/BlobFactory.h"
#include "factories/AudioHeaderFactory.h"
#include "factories/SequenceFactory.h"
#include "factories/SampleFactory.h"
#include "factories/BankFactory.h"
#include "factories/TextureFactory.h"
#include "factories/AnimFactory.h"
#include "n64/Cartridge.h"

#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

void Companion::Init() {

    this->RegisterFactory("AUDIO:HEADER", new AudioHeaderFactory());
    this->RegisterFactory("SEQUENCE", new SequenceFactory());
    this->RegisterFactory("SAMPLE", new SampleFactory());
    this->RegisterFactory("BANK", new BankFactory());

    this->RegisterFactory("TEXTURE", new TextureFactory());
    this->RegisterFactory("BLOB", new BlobFactory());

    // SM64 Specific

    this->RegisterFactory("SM64:ANIM", new AnimFactory());
    this->Process();
}

void Companion::Process() {

    std::ifstream input( this->gRomPath, std::ios::binary );
    this->gRomData = std::vector<uint8_t>( std::istreambuf_iterator<char>( input ), {} );
    input.close();

    N64::Cartridge cartridge = N64::Cartridge(this->gRomData);
    cartridge.Initialize();

    YAML::Node config = YAML::LoadFile("config.yml");

    if(!config[cartridge.GetHash()]){
        std::cout << "No config found for " << cartridge.GetHash() << '\n';
        return;
    }

    auto cfg = config[cartridge.GetHash()];
    auto path = cfg["path"].as<std::string>();
    auto otr = cfg["output"].as<std::string>();

    auto wrapper = SWrapper(otr);

    for (const auto & entry : fs::directory_iterator(path)){
        std::cout << "Processing " << entry.path() << '\n';
        YAML::Node root = YAML::LoadFile(entry.path());
        for(auto asset = root.begin(); asset != root.end(); ++asset){
            auto type = asset->second["type"].as<std::string>();

            LUS::BinaryWriter write = LUS::BinaryWriter();
            RawFactory* factory = this->GetFactory(type);

            if(!factory->process(&write, asset->second, this->gRomData)){
                std::cout << "Failed to process " << asset->first.as<std::string>() << '\n';
                continue;
            }

            auto buffer = write.ToVector();
            auto output = entry.path().stem() / asset->first.as<std::string>();
            std::cout << "Writing " << output << '\n';
            wrapper.CreateFile(output, buffer);

            if(type != "TEXTURE") {
                std::string dpath = "debug/" + path;
                if(!fs::exists(fs::path(dpath).parent_path())){
                    fs::create_directories(fs::path(dpath).parent_path());
                }
                std::ofstream stream(dpath, std::ios::binary);
                stream.write((char*)buffer.data(), buffer.size());
                stream.close();
            }

            std::cout << "Processed " << path << std::endl;

            write.Close();
        }
    }

    MIO0Decoder::ClearCache();
    wrapper.Close();
}

void Companion::RegisterFactory(const std::string &extension, RawFactory *factory) {
    this->gFactories[extension] = factory;
}

RawFactory *Companion::GetFactory(const std::string &extension) {
    if(!this->gFactories.contains(extension)){
        throw std::runtime_error("No factory found for " + extension);
    }

    return this->gFactories[extension];
}
