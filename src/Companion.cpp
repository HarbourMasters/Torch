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
    this->RegisterFactory("SM64:DIALOG", new SDialogFactory());
    this->RegisterFactory("SM64:ANIM", new SAnimFactory());
    this->RegisterFactory("SM64:TEXT", new STextFactory());

    // Debug
    this->RegisterFactory("MIO0", new MIO0Factory());

    this->Process();
}

void Companion::Process() {

    std::ifstream input( this->gRomPath, std::ios::binary );
    this->gRomData = std::vector<uint8_t>( std::istreambuf_iterator<char>( input ), {} );
    input.close();

    this->cartridge = new N64::Cartridge(this->gRomData);
    cartridge->Initialize();

    YAML::Node config = YAML::LoadFile("config.yml");

    if(!config[cartridge->GetHash()]){
        std::cout << "No config found for " << cartridge->GetHash() << '\n';
        return;
    }

    auto cfg = config[cartridge->GetHash()];
    auto path = cfg["path"].as<std::string>();
    auto otr = cfg["output"].as<std::string>();

    auto wrapper = SWrapper(otr);

    auto vWriter = LUS::BinaryWriter();
    vWriter.SetEndianness(LUS::Endianness::Big);
    vWriter.Write((uint8_t) LUS::Endianness::Big);
    vWriter.Write(cartridge->GetCRC());

    for (const auto & entry : fs::directory_iterator(path)){
        std::cout << "Processing " << entry.path() << '\n';
        YAML::Node root = YAML::LoadFile(entry.path().string());
        for(auto asset = root.begin(); asset != root.end(); ++asset){
            auto type = asset->second["type"].as<std::string>();

            LUS::BinaryWriter write = LUS::BinaryWriter();
            RawFactory* factory = this->GetFactory(type);

            if(!factory->process(&write, asset->second, this->gRomData)){
                std::cout << "Failed to process " << asset->first.as<std::string>() << '\n';
                continue;
            }

            auto buffer = write.ToVector();
            auto output = (entry.path().stem() / asset->first.as<std::string>()).string();

            std::replace(output.begin(), output.end(), '\\', '/');

            wrapper.CreateFile(output, buffer);

            if(type != "TEXTURE") {
                std::string dpath = "debug/" + output;
                if(!fs::exists(fs::path(dpath).parent_path())){
                    fs::create_directories(fs::path(dpath).parent_path());
                }
                std::ofstream stream(dpath, std::ios::binary);
                stream.write((char*)buffer.data(), buffer.size());
                stream.close();
            }

            std::cout << "Processed " << output << std::endl;

            write.Close();
        }
    }

    wrapper.CreateFile("version", vWriter.ToVector());
    vWriter.Close();

    MIO0Decoder::ClearCache();
    wrapper.Close();
    this->cartridge = nullptr;
    Companion::Instance = nullptr;
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
