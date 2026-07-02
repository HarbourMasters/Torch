#include <iostream>
#include <memory>
#include "CLI11.hpp"
#include "Companion.h"
#ifdef BK64_SUPPORT
#include "factories/bk64/ConfigFactory.h"
#endif

Companion* Companion::Instance;

#if defined(STANDALONE) && !defined(__EMSCRIPTEN__)

#ifdef BUILD_UI
#include "imgui.h"
#include "ui/BaseBackend.h"
#include "ui/View.h"
#include "ui/list/Main.h"
#include "ui/backends/LusBackend.h"
#include "ui/audio/SequenceDriver.h"

static void LaunchUI() {
    if (Companion::Instance == nullptr) {
        std::cout << "No ROM loaded; nothing to display." << std::endl;
        return;
    }

    auto backend = UI::CreateLusBackend();
    UI::SetBackend(backend.get());

    auto views = std::make_shared<ViewManager>();
    views->SetView(std::make_shared<MainView>());

    // The backend owns the window + frame loop; returns when the user quits.
    backend->RunViewer(views);

    UI::SetBackend(nullptr);
}
#endif

int main(int argc, char* argv[]) {
    CLI::App app{ "Torch - [T]orch is [O]ur [R]esource [C]onversion [H]elper\n\
        * It extracts from a baserom and generates code or an otr.\n\
        * It can also generate an otr from a folder of assets.\n" };
    std::string mode;
    std::string filename;
    std::string target;
    std::string folder;
    std::string archive;
    std::string version;
    ArchiveType otrMode = ArchiveType::None;
    bool otrModeSelected = false;
    bool xmlMode = false;
    bool debug = false;
    std::string srcdir;
    std::string destdir;
    std::vector<std::string> additionalFiles;

    app.require_subcommand();

    /* Generate an OTR */
    const auto otr = app.add_subcommand("otr", "OTR - Generates an otr\n");

    otr->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    otr->add_flag("-v,--verbose", debug, "Verbose Debug Mode");
    otr->add_option("-s,--srcdir", srcdir,
                    "Set source directory to locate config.yml and asset metadata for processing")
        ->check(CLI::ExistingDirectory);
    otr->add_option("-d,--destdir", destdir, "Set destination directory for export");

    otr->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::OTR, debug, srcdir, destdir);
        instance->Init(ExportType::Binary);
    });

    /* Generate an O2R */
    const auto o2r = app.add_subcommand("o2r", "O2R - Generates an o2r\n");

    o2r->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    o2r->add_flag("-v,--verbose", debug, "Verbose Debug Mode");
    o2r->add_option("-s,--srcdir", srcdir,
                    "Set source directory to locate config.yml and asset metadata for processing")
        ->check(CLI::ExistingDirectory);
    o2r->add_option("-d,--destdir", destdir, "Set destination directory for export");
    o2r->add_option("-a,--additional-files", additionalFiles,
                    "Additional files to include in the o2r archive (e.g., mods.toml)")
        ->check(CLI::ExistingFile);
    o2r->add_option("-u,--version", version, "Version to set in the o2r archive");

    o2r->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::O2R, debug, srcdir, destdir);
        instance->SetAdditionalFiles(additionalFiles);
        instance->SetVersion(version);
        instance->Init(ExportType::Binary);
    });

    /* Generate C code */
    const auto code = app.add_subcommand("code", "Code - Generates C code\n");

    code->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    code->add_flag("-v,--verbose", debug, "Verbose Debug Mode; adds offsets to C code");
    code->add_option("-s,--srcdir", srcdir,
                     "Set source directory to locate config.yml and asset metadata for processing")
        ->check(CLI::ExistingDirectory);
    code->add_option("-d,--destdir", destdir, "Set destination directory to place C code to");

    code->parse_complete_callback([&]() {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::None, debug, srcdir, destdir);
        instance->Init(ExportType::Code);
    });

    /* Generate a binary */
    const auto binary = app.add_subcommand("binary", "Binary - Generates a binary\n");

    binary->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    binary
        ->add_option("-s,--srcdir", srcdir,
                     "Set source directory to locate config.yml and asset metadata for processing")
        ->check(CLI::ExistingDirectory);
    binary->add_option("-d,--destdir", destdir, "Set destination directory to place binary to");

    binary->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::None, debug, srcdir, destdir);
        instance->Init(ExportType::Binary);
    });

    /* Generate headers */
    const auto header = app.add_subcommand("header", "Header - Generates headers only\n");

    header->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    header->add_flag("-o,--otr", otrModeSelected, "OTR/O2R Mode");
    header
        ->add_option("-s,--srcdir", srcdir,
                     "Set source directory to locate config.yml and asset metadata for processing")
        ->check(CLI::ExistingDirectory);
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

    pack->add_option("<folder>", folder, "Generate OTR from a directory of assets")
        ->required()
        ->check(CLI::ExistingDirectory);
    pack->add_option("<target>", target, "Archive output destination")->required();
    pack->add_option("<archive-type>", archive, "Archive type: otr or o2r")->required();
    pack->add_option("-u,--version", version, "Version to set in the o2r archive");

    pack->parse_complete_callback([&] {
        if (archive == "otr") {
            otrMode = ArchiveType::OTR;
        } else if (archive == "o2r") {
            otrMode = ArchiveType::O2R;
        } else {
            std::cout << "Invalid archive type" << std::endl;
        }

        if (!folder.empty()) {
            Companion::Pack(folder, target, otrMode, version);
        } else {
            std::cout << "The folder is empty" << std::endl;
        }
    });

#ifdef BK64_SUPPORT
    /* Emit per-slot SHA-1s of the BK64 asset table */
    std::string hashesOut;
    const auto hashes = app.add_subcommand(
        "hashes", "Hashes - Emit per-slot SHA-1 of BK64 asset table for v1.0 baseline\n");

    hashes->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    hashes->add_option("-o,--output", hashesOut, "Output yaml path (default: hashes.yaml in cwd)");

    hashes->parse_complete_callback([&] {
        const std::filesystem::path outPath = hashesOut.empty() ? "hashes.yaml" : hashesOut;
        BK64::EmitAssetHashes(filename, outPath);
    });
#endif

    /* Generate modding files */
    const auto modding_root = app.add_subcommand("modding", "Modding - Generates modding files like png\n");
    const auto modding_import =
        modding_root->add_subcommand("import", "Import - Import modified files to generate C code\n");
    const auto modding_export = modding_root->add_subcommand("export", "Export - Export modified files to a folder\n");

    modding_import->add_option("mode", mode, "code, otr, o2r or header")->required();
    modding_import->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    modding_import->add_flag("-v,--verbose", debug, "Verbose Debug Mode");
    modding_import
        ->add_option(
            "-s,--srcdir", srcdir,
            "Set source directory to locate config.yml and asset metadata for processing, including modified files")
        ->check(CLI::ExistingDirectory);
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
    modding_export
        ->add_option(
            "-s,--srcdir", srcdir,
            "Set source directory to locate config.yml and asset metadata for processing, including modified files")
        ->check(CLI::ExistingDirectory);
    modding_export->add_option("-d,--destdir", destdir,
                               "Set destination directory to place for generating modified files");

    modding_export->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::None, debug, srcdir, destdir);
        if (xmlMode) {
            instance->Init(ExportType::XML);
        } else {
            instance->Init(ExportType::Modding);
        }
    });

#ifdef BUILD_UI
    /* Launch the interactive viewer */
    const auto ui = app.add_subcommand("ui", "UI - Parses a baserom and opens the interactive asset viewer\n");

    ui->add_option("<baserom.z64>", filename, "")->required()->check(CLI::ExistingFile);
    ui->add_flag("-v,--verbose", debug, "Verbose Debug Mode");
    ui->add_option("-s,--srcdir", srcdir,
                   "Set source directory to locate config.yml and asset metadata for processing")
        ->check(CLI::ExistingDirectory);

    ui->parse_complete_callback([&] {
        const auto instance = Companion::Instance = new Companion(filename, ArchiveType::None, debug, srcdir, destdir);
        // Parse-only: populate the asset results for the viewer without exporting
        // anything (shouldProcess = false skips all writes). Export type is
        // irrelevant since nothing is exported.
        std::atomic<size_t> assetCount{ 0 };
        instance->Init(ExportType::Binary, assetCount, false);
        if (std::getenv("TORCH_SEQ_TEST") != nullptr) { // headless driver check, no window
            for (const auto& [file, results] : instance->GetParseResults()) {
                for (const auto& item : results) {
                    if (item.type.find("SEQUENCE") == std::string::npos || !item.data.has_value()) {
                        continue;
                    }
                    auto* driver = UI::GetSequenceDriver(item.type);
                    if (driver == nullptr) {
                        continue;
                    }
                    UI::RenderedAudio out;
                    const bool ok = driver->Render(item, 0, out);
                    float peak = 0.0f;
                    double sumSq = 0.0;
                    size_t loud = 0;
                    for (const auto v : out.pcm) {
                        const float f = std::fabs((float)v) / 32768.0f;
                        peak = std::max(peak, f);
                        sumSq += (double)f * f;
                        loud += f > 0.01f ? 1 : 0;
                    }
                    const double rms = out.pcm.empty() ? 0.0 : std::sqrt(sumSq / (double)out.pcm.size());
                    printf("[seqtest] %s ok=%d notes=%zu frames=%zu peak=%.3f rms=%.4f loud=%.1f%%\n",
                           item.name.c_str(), ok, out.noteCount, out.pcm.size() / 2, peak, rms,
                           out.pcm.empty() ? 0.0 : 100.0 * (double)loud / (double)out.pcm.size());
                    if (const char* wavDir = std::getenv("TORCH_SEQ_WAV")) {
                        std::string base = item.name;
                        std::replace(base.begin(), base.end(), '/', '_');
                        const std::string wavPath = std::string(wavDir) + "/" + base + ".wav";
                        FILE* f = fopen(wavPath.c_str(), "wb");
                        if (f != nullptr) {
                            const uint32_t dataSize = (uint32_t)(out.pcm.size() * 2);
                            const uint32_t riffSize = 36 + dataSize;
                            const uint32_t rate = 32000, byteRate = rate * 4;
                            const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
                            const uint32_t fmtSize = 16;
                            fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f); fwrite("WAVE", 1, 4, f);
                            fwrite("fmt ", 1, 4, f); fwrite(&fmtSize, 4, 1, f); fwrite(&fmt, 2, 1, f);
                            fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
                            fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
                            fwrite("data", 1, 4, f); fwrite(&dataSize, 4, 1, f);
                            fwrite(out.pcm.data(), 2, out.pcm.size(), f);
                            fclose(f);
                        }
                    }
                }
            }
            return;
        }
        LaunchUI();
    });
#endif

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
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
