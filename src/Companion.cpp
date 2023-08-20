#include "Companion.h"

#include "storm/SWrapper.h"
#include "utils/MIODecoder.h"
#include "factories/RawFactory.h"
#include "factories/BlobFactory.h"
#include "factories/TextureFactory.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

static const std::unordered_map<std::string, RawFactory*> gFactories = {
    { ".png", new TextureFactory() },
    { ".bin", new BlobFactory(LUS::ResourceType::Blob) },
    { ".sbox", new BlobFactory(LUS::ResourceType::Blob, true) },
};

void Companion::Start() {
    std::cout << "Super Mario 64 Rom Path: " << this->gRomPath << '\n';

    std::ifstream input( this->gRomPath, std::ios::binary );
    this->gRomData = std::vector<uint8_t>( std::istreambuf_iterator<char>( input ), {} );
    input.close();

    // TODO: Validate hash

    std::cout << "Detected Rom Size: " << this->gRomData.size() << '\n';

    this->ProcessAssets();
}

void Companion::ProcessAssets() {
    json assets = json::parse( std::ifstream( "assets.json" ) );

    SWrapper wrapper = SWrapper("smcube.otr");

    for( auto& [asset, data] : assets.items() ) {
        std::string extension = fs::path(asset).extension().string();
        if( gFactories.find(extension) == gFactories.end() ) {
            std::cout << "No factory found for " << asset << '\n';
            continue;
        }
        LUS::BinaryWriter write = LUS::BinaryWriter();
        std::string path = asset.substr(0, asset.find_last_of('.'));
        json entry = {
            { "path", path },
            { "offsets", data }
        };
        RawFactory* factory = gFactories.at(extension);

        if(!factory->process(&write, entry, this->gRomData)){
            std::cout << "Failed to process " << asset << '\n';
            continue;
        }

	    std::cout << "Processed " << path << std::endl;
        bool isTexture = typeid(*factory) == typeid(TextureFactory);

        auto buffer = write.ToVector();
        // TODO: Change this that we all know its going to crash with other assets
        wrapper.CreateFile(isTexture ? path.substr(0, path.find_last_of('.')) : path, buffer);

        if(!isTexture) {
            // Write to file too
            std::string dpath = "debug/" + path;
            fs::create_directories(fs::path(dpath).parent_path());
            std::ofstream output(dpath, std::ios::binary);
            output.write((char*)buffer.data(), buffer.size());
            output.close();
        }

        write.Close();
    }

    MIO0Decoder::ClearCache();
    wrapper.Close();
}