#include <iostream>
#include "CLI11.hpp"
#include "Companion.h"

#ifdef STANDALONE
Companion* Companion::Instance;

int main(int argc, char *argv[]) {
    CLI::App app{"CubeOS - Rom extractor"};

    std::string filename;
    app.add_option("path", filename, "sm64 us rom")->required()->check(CLI::ExistingFile);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    Companion::Instance = new Companion(filename);
    Companion::Instance->Init();
    return 0;
}
#endif