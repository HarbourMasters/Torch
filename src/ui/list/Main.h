#pragma once

#include <string>
#include <filesystem>
#include <optional>

#include "ui/View.h"
#include "ui/Scroll.h"
#include "utils/TorchUtils.h"

namespace fs = std::filesystem;

class MainView : public View {
public:
    std::optional<fs::path> configFilePath = std::nullopt;
    std::optional<fs::path> romFilePath = std::nullopt;
    std::optional<fs::path> selectedFile = std::nullopt;
    std::vector<fs::path> files;

    std::vector<float> item_heights;
    std::vector<float> item_y_positions;
    float total_height = 0.0f;

    void Update() override {
        ImGuiIO& io = ImGui::GetIO();
        float width = io.DisplaySize.x;
        float height = io.DisplaySize.y;

        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2((width / 2) - 300, (height / 2) - 200), ImGuiCond_Always);
        if (ImGuiFileDialog::Instance()->Display("ChooseRom")) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                romFilePath = fs::path(ImGuiFileDialog::Instance()->GetFilePathName());
                printf("Selected ROM: %s\n", romFilePath->string().c_str());
            }

            // close
            ImGuiFileDialog::Instance()->Close();
        }
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2((width / 2) - 300, (height / 2) - 200), ImGuiCond_Always);
        if (ImGuiFileDialog::Instance()->Display("ChooseConfigYML")) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                fs::path file = fs::path(ImGuiFileDialog::Instance()->GetFilePathName());
                if (file.filename() == "config.yml") {
                    configFilePath = file;
                }
                printf("Selected Config: %s\n", configFilePath->string().c_str());
            }

            // close
            ImGuiFileDialog::Instance()->Close();
        }
    }

    void Render() override {
        ImGuiIO& io = ImGui::GetIO();
        float width = io.DisplaySize.x;
        float height = io.DisplaySize.y;

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        ImGui::Begin("Torch GUI", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
        DrawToolbar();
        ImGui::BeginChild("FilesPanel", ImVec2(236, height - 45), true, ImGuiWindowFlags_HorizontalScrollbar);
        {
            ImGui::Text("Files");
            ImGui::Separator();
            for (int i = 0; i < files.size(); i++) {
                if (ImGui::Selectable(files[i].filename().c_str(), selectedFile == files[i])) {
                    selectedFile = files[i];
                    RecalculateLayout();
                }
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("AssetsPanel", ImVec2(0, height - 45), true);
        {
            ImGui::Text("Assets");
            ImGui::Separator();
            if(selectedFile.has_value()) {
                auto parseData = Companion::Instance->GetParseResults();
                auto items = parseData[selectedFile.value().string()];
                ImGuiListClipper clipper;

                float cursorY = ImGui::GetCursorPosY();
                clipper.Begin(total_height, 1.0f);
                while (clipper.Step()) {
                    auto it_start = std::lower_bound(item_y_positions.begin(), item_y_positions.end(), clipper.DisplayStart);
                    int start_index = std::distance(item_y_positions.begin(), it_start);
                    if (it_start != item_y_positions.begin()) {
                        start_index--;
                    }

                    for (int i = start_index; i < items.size(); ++i) {
                        const float item_top = item_y_positions[i];
                        const float item_bottom = item_top + item_heights[i];

                        if (item_top >= clipper.DisplayEnd) {
                            break;
                        }

                        if (item_bottom >= clipper.DisplayStart) {
                            ImGui::SetCursorPosY(cursorY + item_top);
                            auto& asset = items[i];
                            auto ui = Companion::Instance->GetUIFactory(asset.type);
                            if(!ui.has_value() || !asset.data.has_value()) {
                                ImGui::Text("No UI available for asset type: %s", asset.type.c_str());
                            } else {
                                ui.value()->DrawUI(asset);
                            }
                            ImGui::Separator();
                        }
                    }
                }
                clipper.End();
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void DrawToolbar() {
        const ImVec2 button = ImVec2(25, 25);
        if(ImGui::Button(ICON_FA_FILE, button)) {
            IGFD::FileDialogConfig config;
            config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("ChooseConfigYML", "Choose Config File", ".yml", config);
        }
        ImGui::SameLine();
        if(ImGui::Button(ICON_FA_FILE_IMPORT, button)) {
            IGFD::FileDialogConfig config;
            config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("ChooseRom", "Choose ROM File", ".z64", config);
        }
        ImGui::SameLine();
        if(ImGui::Button(ICON_FA_PLAY, button)) {
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
    }

    void ReloadScrollFiles() {
        files.clear();
        const auto entries = Companion::Instance->GetAddrMap();
        for (const auto & entry : entries){
            files.push_back(fs::path(entry.first));
        }
    }

    void RecalculateLayout() {
        item_heights.clear();
        item_y_positions.clear();
        total_height = 0.0f;
        auto parseData = Companion::Instance->GetParseResults();
        auto items = parseData[selectedFile.value().string()];

        for (const auto& asset : items) {
            auto ui = Companion::Instance->GetUIFactory(asset.type);
            float y = !ui.has_value() || !asset.data.has_value() ? ImGui::GetTextLineHeightWithSpacing() : ui.value()->GetItemHeight(asset);
            y += ImGui::GetStyle().ItemSpacing.y * 2;
            item_heights.push_back(y);
        }

        float current_y = 0.0f;
        for (float height : item_heights) {
            item_y_positions.push_back(current_y);
            current_y += height;
        }
        total_height = current_y;
    }
};