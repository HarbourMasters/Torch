#pragma once

#include <string>
#include <filesystem>
#include <optional>

#include "ui/View.h"
#include "ui/Scroll.h"
#include "utils/TorchUtils.h"

namespace fs = std::filesystem;

enum class FileDialogMode {
    OpenConfigFile,
    OpenROMFile,
    None
};

class MainView : public View {
public:
    ScrollComponent<fs::path> files;
    ScrollComponent<ParseResultData> assets;

    GuiWindowFileDialogState fileDialogState;
    FileDialogMode fileDialogMode = FileDialogMode::None;

    std::optional<fs::path> configFilePath = "/Volumes/Moon/dot/Lywx/sf64-progress/config.yml";
    std::optional<fs::path> romFilePath = "/Volumes/Moon/dot/Lywx/sf64-progress/baserom.us.rev1.uncompressed.z64";

    void Init() override {
        fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());
        fileDialogState.supportDrag = false;

        files.rect = { 0, 50, 230, GetScreenHeight() - 50 };
        files.title = "Files";
        files.emptyText = "No files loaded";
        files.drawItem = [](Vector2 pos, bool show, const fs::path& item, bool isSelected, bool* isClicked) {
            if(show) {
                Rectangle bounds = { pos.x + 10, pos.y, 205, 30 };
                if(isSelected) {
                    GuiPanel((Rectangle){ 5, pos.y, bounds.width, bounds.height }, NULL);
                }
                *isClicked = GuiLabelButton(bounds, GuiIconText(ICON_FILE, item.filename().string().c_str())) == 1;
            }
            return (Vector2) { 0, 30 };
        };

        assets.rect = { 236, 50, GetScreenWidth() - 240, GetScreenHeight() - 50 };
        assets.title = "Assets";
        assets.emptyText = "No asset selected";
        assets.drawItem = [](Vector2 pos, bool show, const ParseResultData& item, bool isSelected, bool* isClicked) {
            Vector2 window = { GetScreenWidth() - 200, GetScreenHeight() - 50};
            auto ui = Companion::Instance->GetUIFactory(item.type).value();
            if(show){
                *isClicked = ui->DrawUI(pos, window, item);
            }
            return ui->GetBounds(window, item);
        };
    }

    void Update() override {
        if(fileDialogState.SelectFilePressed) {
            fs::path file = fs::path(fileDialogState.dirPathText) / fs::path(fileDialogState.fileNameText);
            switch (fileDialogMode) {
                case FileDialogMode::OpenConfigFile: {
                    if (file.filename() != "config.yml") {
                        return;
                    }
                    configFilePath = file;
                    printf("Selected config.yml: %s\n", file.string().c_str());
                    break;
                }
                case FileDialogMode::OpenROMFile: {
                    if (file.extension() != ".z64") {
                        return;
                    }
                    romFilePath = file;
                    printf("Selected ROM File: %s\n", file.string().c_str());
                    break;
                }
                default:
                    break;
            }

            fileDialogState.SelectFilePressed = false;
        }

        if(files.selected != -1) {
            if(assets.init) return;
            auto selectedFile = files.items[files.selected];
            auto parseData = Companion::Instance->GetParseResults();
            for(auto& asset : parseData[selectedFile.string()]) {
                auto ui = Companion::Instance->GetUIFactory(asset.type);
                if(!ui.has_value() || !asset.data.has_value()) {
                    continue;
                }
                assets.items.push_back(asset);
            }
            assets.init = true;
        } else {
            assets.items.clear();
            assets.selected = -1;
            assets.init = false;
        }
    }

    void Render() override {
        int width = GetScreenWidth();
        int height = GetScreenHeight();

        if (fileDialogState.windowActive) GuiLock();

        DrawToolbar();

        GuiCustomScrollPanel<fs::path>(files);
        GuiCustomScrollPanel<ParseResultData>(assets);

        GuiUnlock();

        // GUI: Dialog Window
        //--------------------------------------------------------------------------------
        GuiWindowFileDialog(&fileDialogState);
        //--------------------------------------------------------------------------------
    }

    void DrawToolbar() {
        int width = GetScreenWidth();
        int height = GetScreenHeight();

        GuiPanel((Rectangle){ 0, 0, (float) width, 50 }, NULL);
        GuiPanel((Rectangle){ 0, 0, 130, 50 }, NULL);

        GuiSetTooltip("Open config.yml (LCTRL+S)");
        if (GuiButton((Rectangle){ 10, 10, 30, 30 }, GuiIconText(ICON_FOLDER_OPEN, NULL))) {
            fileDialogState.windowActive = true;
            fileDialogState.title = "Open config.yml";
            fileDialogMode = FileDialogMode::OpenConfigFile;
        }
        GuiSetTooltip("Open ROM File (LCTRL+O)");
        if (GuiButton((Rectangle){ 50, 10, 30, 30 }, GuiIconText(ICON_FILE_OPEN, NULL))) {
            fileDialogState.windowActive = true;
            fileDialogState.title = "Open ROM File";
            fileDialogMode = FileDialogMode::OpenROMFile;
        }
        GuiSetTooltip("Run Torch (LCTRL+R)");
        if (GuiButton((Rectangle){ 90, 10, 30, 30 }, GuiIconText(ICON_PLAYER_PLAY, NULL))) {
            if (configFilePath.has_value() && romFilePath.has_value()) {
                // Launch Torch
                const auto instance = Companion::Instance = new Companion(romFilePath->string(), ArchiveType::O2R, false, configFilePath->parent_path().string(), "");
                instance->Init(ExportType::Binary, false);
                instance->Process();
                ReloadScrollFiles();
            } else {
                printf("Please select both a config.yml and a ROM file before running Torch.\n");
            }
        }
        GuiSetTooltip(NULL);
    }

    void ReloadScrollFiles() {
        files.items.clear();
        const auto entries = Companion::Instance->GetAddrMap();
        for (const auto & entry : entries){
            files.items.push_back(fs::path(entry.first));
        }
    }
};