#include <iostream>
#include "CLI11.hpp"
#include "Companion.h"

#ifdef STANDALONE
Companion* Companion::Instance;

int main(int argc, char *argv[]) {
    CLI::App app{"Torch - [T]orch is [O]ur [R]esource [C]onversion [H]elper\n\
        * It extracts from a baserom and generates code or an otr.\n\
        * It can also generate an otr from a folder of assets.\n"
    };

    app.require_subcommand();

    /**
     * Generate OTR or C Code
     * 
     * ./cubeos generate otr baserom.z64
     * ./cubeos generate code baserom.z64
     * 
    **/
    //auto otr = app.add_subcommand("otr", "Generates an otr");
    std::string mode;
    std::string filename;
    std::string target;
    std::string folder;
    bool otrMode = false;
    bool debug = false;
    bool header = false;

    /* Generate an OTR */
    auto otr = app.add_subcommand("otr", "OTR - Generates an otr\n");

    otr->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);

    otr->add_flag("-t,--target", target, "OTR output destination");
    otr->add_flag("-o,--otr", otrMode, "OTR Mode");
    otr->add_flag("-d,--dir", folder, "Generate OTR from a directory of assets");
    otr->add_flag("-z,--zeader", header, "Generates a header");
    otr->add_flag("-v,--verbose", debug, "Verbose Debug Mode");
    otr->parse_complete_callback([&]() {
        auto instance = Companion::Instance = new Companion(filename, otrMode, debug);

        if (header) {
            instance->Init(ExportType::Header);
        } else {
            instance->Init(ExportType::Binary);
        }
        
        if (!folder.empty()) {
                Companion::Pack(folder, target);
        }

    });

    /* Generate C code */
    auto code = app.add_subcommand("code", "Code - Generates C code\n");

    code->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);

    code->add_flag("-v,--verbose", debug, "Verbose Debug Mode; adds offsets to C code");
    code->parse_complete_callback([&]() {
        auto instance = Companion::Instance = new Companion(filename, otrMode, debug);
        instance->Init(ExportType::Code);
    });

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::cout << app.help() << std::endl;
        return app.exit(e);
    }

    // No arguments --> display help.
    if (argc == 1) {
        std::cout << app.help() << std::endl;
    }

    return 0;
}
#endif