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
    std::string mode;
    std::string filename;
    std::string target;
    std::string folder;
    bool otrMode = false;
    bool debug = false;

    app.require_subcommand();

    /* Generate an OTR */
    const auto otr = app.add_subcommand("otr", "OTR - Generates an otr\n");

    otr->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    otr->add_flag("-v,--verbose", debug, "Verbose Debug Mode");

    otr->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, true, debug);
        instance->Init(ExportType::Binary);
    });

    /* Generate C code */
    const auto code = app.add_subcommand("code", "Code - Generates C code\n");

    code->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    code->add_flag("-v,--verbose", debug, "Verbose Debug Mode; adds offsets to C code");

    code->parse_complete_callback([&]() {
        const auto instance = Companion::Instance = new Companion(filename, false, debug);
        instance->Init(ExportType::Code);
    });

    /* Generate a binary */
    const auto binary = app.add_subcommand("binary", "Binary - Generates a binary\n");

    binary->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);

    binary->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, false, debug);
        instance->Init(ExportType::Binary);
    });

    /* Generate headers */
    const auto header = app.add_subcommand("header", "Header - Generates headers only\n");

    header->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    header->add_flag("-o,--otr", otrMode, "OTR Mode");

    header->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, otrMode, debug);
        instance->Init(ExportType::Header);
    });

    /* Pack an otr from a folder */
    const auto pack = app.add_subcommand("pack", "Pack - Packs an otr from a folder\n");

    pack->add_option("<folder>", folder, "Generate OTR from a directory of assets")->required()->check(CLI::ExistingDirectory);
    pack->add_option("<target>", target, "OTR output destination")->required();

    pack->parse_complete_callback([&] {
        if (!folder.empty()) {
            Companion::Pack(folder, target);
        } else {
            std::cout << "The folder is empty" << std::endl;
        }
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