#include <iostream>
#include "CLI11.hpp"
#include "Companion.h"

Companion* Companion::Instance;

#ifdef STANDALONE

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

    /* Generate modding files */
    const auto modding_root = app.add_subcommand("modding", "Modding - Generates modding files like png\n");
    const auto modding_import = modding_root->add_subcommand("import", "Import - Import modified files to generate C code\n");
    const auto modding_export = modding_root->add_subcommand("export", "Export - Export modified files to a folder\n");

    modding_import->add_option("mode", mode, "code, binary or header")->required();
    modding_import->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);

    modding_import->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, false, debug, true);
        if (mode == "code") {
            instance->Init(ExportType::Code);
        } else if (mode == "binary") {
            instance->Init(ExportType::Binary);
        } else if (mode == "header") {
            instance->Init(ExportType::Header);
        } else {
            std::cout << "Invalid mode" << std::endl;
        }
    });

    modding_export->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);

    modding_export->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, false, debug);
        instance->Init(ExportType::Modding);
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