#include <iostream>
#include "CLI11.hpp"
#include "Companion.h"

#ifdef STANDALONE
Companion* Companion::Instance;

void BindOTRMode(CLI::App& app){
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
}

void BindPackMode(CLI::App& app){
    auto pack = app.add_subcommand("pack", "Packs the rom from a folder");
    std::string folder;
    pack->add_option("folder", folder, "Folder")->required()->check(CLI::ExistingDirectory);
    std::string output;
    pack->add_option("output", output, "MPQ Output")->required();
    pack->parse_complete_callback([&]() {
        Companion::Pack(folder, output);
    });
}

int main(int argc, char *argv[]) {
    CLI::App app{"CubeOS - Rom extractor"};

    BindOTRMode(app);
    BindPackMode(app);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    return 0;
}
#endif