#include <iostream>
#include "CLI11.hpp"
#include "Companion.h"

#if defined(STANDALONE) && !defined(__EMSCRIPTEN__)
Companion* Companion::Instance;

#ifdef BUILD_UI
#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"

#define RAYGUI_IMPLEMENTATION
#include "ui/View.h"
#include "ui/list/Main.h"

void ApplyRayguiTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // Set all rounding to 0.0f for sharp corners
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.GrabRounding      = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding       = 0.0f;

    // Set all border sizes to 1.0f for a consistent outline
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.TabBorderSize     = 1.0f;

    // Tweak spacing to be compact like raygui
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.ScrollbarSize     = 12.0f;

    // Main Colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.25f, 0.25f, 0.25f, 1.00f); // Dark Gray
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.96f, 0.96f, 0.96f, 1.00f); // Almost White
    colors[ImGuiCol_ChildBg]                = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);
    colors[ImGuiCol_Border]                 = ImVec4(0.56f, 0.56f, 0.56f, 1.00f); // Mid Gray
    colors[ImGuiCol_BorderShadow]           = ImVec4(1.00f, 1.00f, 1.00f, 0.20f); // Transparent white for a subtle inner highlight

    // Frame/Control Colors
    colors[ImGuiCol_FrameBg]                = ImVec4(0.78f, 0.78f, 0.78f, 1.00f); // Lighter Gray
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.78f, 0.82f, 0.90f, 1.00f); // Light Blue
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.61f, 0.67f, 0.78f, 1.00f); // Darker Blue

    // Title Bar
    colors[ImGuiCol_TitleBg]                = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.78f, 0.82f, 0.90f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    
    // Buttons
    colors[ImGuiCol_Button]                 = colors[ImGuiCol_FrameBg];
    colors[ImGuiCol_ButtonHovered]          = colors[ImGuiCol_FrameBgHovered];
    colors[ImGuiCol_ButtonActive]           = colors[ImGuiCol_FrameBgActive];

    // Headers (Selectable, CollapsingHeader, etc.)
    colors[ImGuiCol_Header]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = colors[ImGuiCol_FrameBgHovered];
    colors[ImGuiCol_HeaderActive]           = colors[ImGuiCol_FrameBgActive];
    
    // Tabs
    colors[ImGuiCol_Tab]                    = colors[ImGuiCol_Header];
    colors[ImGuiCol_TabHovered]             = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_TabActive]              = colors[ImGuiCol_HeaderActive];
    colors[ImGuiCol_TabUnfocused]           = colors[ImGuiCol_TitleBg];
    colors[ImGuiCol_TabUnfocusedActive]     = colors[ImGuiCol_TitleBgActive];

    // Checkbox and Sliders
    colors[ImGuiCol_CheckMark]              = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.56f, 0.56f, 0.56f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    
    // Separators and Resize Grips
    colors[ImGuiCol_Separator]              = colors[ImGuiCol_Border];
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.85f, 0.85f, 0.85f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.78f, 0.82f, 0.90f, 0.78f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.61f, 0.67f, 0.78f, 1.00f);
}

void launchUI() {
    const int screenWidth = 800;
    const int screenHeight = 600;
    std::shared_ptr<ViewManager> view = std::make_shared<ViewManager>();

    InitWindow(screenWidth, screenHeight, "Torch GUI");
    SetTargetFPS(60);

    view->SetView(std::make_shared<MainView>());

    rlImGuiSetup(false);

    ApplyRayguiTheme();

    while (!WindowShouldClose()) {
        BeginDrawing();
            ClearBackground(RAYWHITE);
            rlImGuiBegin();
                view->Render();
            rlImGuiEnd();
        EndDrawing();
    }

    CloseWindow();
}
#endif

int main(int argc, char *argv[]) {
    CLI::App app{"Torch - [T]orch is [O]ur [R]esource [C]onversion [H]elper\n\
        * It extracts from a baserom and generates code or an otr.\n\
        * It can also generate an otr from a folder of assets.\n"
    };
    std::string mode;
    std::string filename;
    std::string target;
    std::string folder;
    std::string archive;
    ArchiveType otrMode = ArchiveType::None;
    bool otrModeSelected = false;
    bool xmlMode = false;
    bool debug = false;
    std::string srcdir;
    std::string destdir;

    app.require_subcommand();

    /* Generate an OTR */
    const auto otr = app.add_subcommand("otr", "OTR - Generates an otr\n");

    otr->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    otr->add_flag("-v,--verbose", debug, "Verbose Debug Mode");
    otr->add_option("-s,--srcdir", srcdir, "Set source directory to locate config.yml and asset metadata for processing")->check(CLI::ExistingDirectory);
    otr->add_option("-d,--destdir", destdir, "Set destination directory for export");

    otr->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::OTR, debug, srcdir, destdir);
        instance->Init(ExportType::Binary);
    });

    /* Generate an O2R */
    const auto o2r = app.add_subcommand("o2r", "O2R - Generates an o2r\n");

    o2r->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    o2r->add_flag("-v,--verbose", debug, "Verbose Debug Mode");
    o2r->add_option("-s,--srcdir", srcdir, "Set source directory to locate config.yml and asset metadata for processing")->check(CLI::ExistingDirectory);
    o2r->add_option("-d,--destdir", destdir, "Set destination directory for export");

    o2r->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::O2R, debug, srcdir, destdir);
        instance->Init(ExportType::Binary);
    });

    /* Generate C code */
    const auto code = app.add_subcommand("code", "Code - Generates C code\n");

    code->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    code->add_flag("-v,--verbose", debug, "Verbose Debug Mode; adds offsets to C code");
    code->add_option("-s,--srcdir", srcdir, "Set source directory to locate config.yml and asset metadata for processing")->check(CLI::ExistingDirectory);
    code->add_option("-d,--destdir", destdir, "Set destination directory to place C code to");

    code->parse_complete_callback([&]() {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::None, debug, srcdir, destdir);
        instance->Init(ExportType::Code);
    });

    /* Generate a binary */
    const auto binary = app.add_subcommand("binary", "Binary - Generates a binary\n");

    binary->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    binary->add_option("-s,--srcdir", srcdir, "Set source directory to locate config.yml and asset metadata for processing")->check(CLI::ExistingDirectory);
    binary->add_option("-d,--destdir", destdir, "Set destination directory to place binary to");

    binary->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::None, debug, srcdir, destdir);
        instance->Init(ExportType::Binary);
        instance->Process();
    });

    /* Generate headers */
    const auto header = app.add_subcommand("header", "Header - Generates headers only\n");

    header->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    header->add_flag("-o,--otr", otrModeSelected, "OTR/O2R Mode");
    header->add_option("-s,--srcdir", srcdir, "Set source directory to locate config.yml and asset metadata for processing")->check(CLI::ExistingDirectory);
    header->add_option("-d,--destdir", destdir, "Set destination directory to place headers to");

    header->parse_complete_callback([&] {
        if (otrModeSelected) {
            otrMode = ArchiveType::OTR;
        } else {
            otrMode = ArchiveType::None;
        }

        const auto instance = Companion::Instance = new Companion(filename, otrMode, debug, srcdir, destdir);
        instance->Init(ExportType::Header);
    });

    /* Pack an archive from a folder */
    const auto pack = app.add_subcommand("pack", "Pack - Packs an archive from a folder\n");

    pack->add_option("<folder>", folder, "Generate OTR from a directory of assets")->required()->check(CLI::ExistingDirectory);
    pack->add_option("<target>", target, "Archive output destination")->required();
    pack->add_option("<archive-type>", archive, "Archive type: otr or o2r")->required();

    
    pack->parse_complete_callback([&] {
        if (archive == "otr") {
            otrMode = ArchiveType::OTR;
        } else if (archive == "o2r") {
            otrMode = ArchiveType::O2R;
        } else {
            std::cout << "Invalid archive type" << std::endl;
        }

        if (!folder.empty()) {
            Companion::Pack(folder, target, otrMode);
        } else {
            std::cout << "The folder is empty" << std::endl;
        }
    });

    /* Generate modding files */
    const auto modding_root = app.add_subcommand("modding", "Modding - Generates modding files like png\n");
    const auto modding_import = modding_root->add_subcommand("import", "Import - Import modified files to generate C code\n");
    const auto modding_export = modding_root->add_subcommand("export", "Export - Export modified files to a folder\n");

    modding_import->add_option("mode", mode, "code, otr, o2r or header")->required();
    modding_import->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    modding_import->add_flag("-v,--verbose", debug, "Verbose Debug Mode");
    modding_import->add_option("-s,--srcdir", srcdir, "Set source directory to locate config.yml and asset metadata for processing, including modified files")->check(CLI::ExistingDirectory);
    modding_import->add_option("-d,--destdir", destdir, "Set destination directory to place for generating C code");

    modding_import->parse_complete_callback([&] {
        ArchiveType otrMode;

        if (mode == "otr") {
            otrMode = ArchiveType::OTR;
        } else if (mode == "o2r") {
            otrMode = ArchiveType::O2R;
        } else {
            otrMode = ArchiveType::None;
        }

        const auto instance = Companion::Instance = new Companion(filename, otrMode, debug, true, srcdir, destdir);
        if (mode == "code") {
            instance->Init(ExportType::Code);
        } else if (mode == "otr" || mode == "o2r") {
            instance->Init(ExportType::Binary);
        } else if (mode == "header") {
            instance->Init(ExportType::Header);
        } else {
            std::cout << "Invalid mode" << std::endl;
        }
    });

    modding_export->add_flag("-x,--xml", xmlMode, "XML Mode");
    modding_export->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    modding_export->add_option("-s,--srcdir", srcdir, "Set source directory to locate config.yml and asset metadata for processing, including modified files")->check(CLI::ExistingDirectory);
    modding_export->add_option("-d,--destdir", destdir, "Set destination directory to place for generating modified files");

    modding_export->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::None, debug, srcdir, destdir);
        if (xmlMode) {
            instance->Init(ExportType::XML);
        } else {
            instance->Init(ExportType::Modding);
        }
    });

#ifdef BUILD_UI
    const auto ui = app.add_subcommand("ui", "UI - Launch the GUI (if built with UI support)");
    ui->parse_complete_callback(launchUI);
#endif

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
