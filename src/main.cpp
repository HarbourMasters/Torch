#include <iostream>
#include "CLI11.hpp"
#include "Companion.h"

#ifdef STANDALONE
Companion* Companion::Instance;

int main(int argc, char *argv[]) {
    CLI::App app{"CubeOS - Rom extractor"};


    /**
     * Generate OTR or C Code
     * 
     * ./cubeos generate otr baserom.z64
     * ./cubeos generate code baserom.z64
     * 
    **/
    auto otr = app.add_subcommand("generate", "Extracts the rom");
    std::string mode;
    std::string filename;
    bool otrMode = false;
    otr->add_option("mode", mode, "<extract_mode> modes: code, otr")->required();
    otr->add_option("rom", filename, "z64 rom")->required()->check(CLI::ExistingFile);
    otr->add_flag("-o,--otr", otrMode, "OTR Mode");
    otr->parse_complete_callback([&]() {
        auto instance = Companion::Instance = new Companion(filename, otrMode);
        if(mode == "header") {
            instance->Init(ExportType::Header);
        } else if(mode == "code") {
            instance->Init(ExportType::Code);
        } else if(mode == "binary") {
            instance->Init(ExportType::Binary);
        } else {
            std::cout << "Invalid mode: " << mode << std::endl;
        }
    });

    /**
     * Generate OTR from a folder
     * 
     * ./cubeos pack <folder_dir> <otr output>
     * 
    **/
    auto pack = app.add_subcommand("pack", "Packs the rom from a folder");
    std::string folder;
    std::string output;
    pack->add_option("folder", folder, "Folder")->required()->check(CLI::ExistingDirectory);
    pack->add_option("output", output, "MPQ Output")->required();
    pack->parse_complete_callback([&]() {
        Companion::Pack(folder, output);
    });

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    return 0;
}
#endif