#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ui/View.h"
#include "Companion.h"

// File list on the left, assets of the selected file on the right. Each asset is
// drawn by its factory's BaseFactoryUI, or a plain text row if it has none.
class MainView : public View {
public:
    std::optional<std::string> selectedFile = std::nullopt;
    std::vector<std::string> files;
    char filter[256] = {};

    // A directory in the file tree: child folders keyed by name, plus the files
    // directly inside it as (filename, full path) pairs.
    struct FileNode {
        std::map<std::string, FileNode> dirs;
        std::vector<std::pair<std::string, std::string>> files;
    };
    FileNode tree;

    void Init() override {
        ReloadFiles();
    }

    void ReloadFiles() {
        files.clear();
        for (const auto& [file, assets] : Companion::Instance->GetParseResults()) {
            files.push_back(file);
        }
        std::sort(files.begin(), files.end());
        BuildTree();
    }

    void Render() override {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
        ImGui::Begin("Torch GUI", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        DrawHeader();
        ImGui::Spacing();

        DrawFilesPanel();
        ImGui::SameLine();
        DrawAssetsPanel();

        ImGui::End();
    }

private:
    void DrawHeader() {
        ImGui::PushFont(nullptr);
        ImGui::TextColored(ImVec4(0.93f, 0.49f, 0.20f, 1.00f), "TORCH");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("resource viewer");
        ImGui::SameLine();
        char status[96];
        snprintf(status, sizeof(status), "%zu files   %.0f fps (%.1f ms)", files.size(),
                 ImGui::GetIO().Framerate, 1000.0f / std::max(ImGui::GetIO().Framerate, 0.001f));
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(status).x);
        ImGui::TextDisabled("%s", status);
        ImGui::Separator();
    }

    void DrawFilesPanel() {
        ImGui::BeginChild("FilesPanel", ImVec2(300, 0), true);
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter", "Search files...", filter, sizeof(filter));
            ImGui::Separator();

            ImGui::BeginChild("FilesList");
            DrawTree(tree);
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }

    void DrawAssetsPanel() {
        ImGui::BeginChild("AssetsPanel", ImVec2(0, 0), true);
        {
            if (selectedFile.has_value()) {
                const auto* assets = SelectedAssets();
                ImGui::Text("%s", fs::path(selectedFile.value()).filename().string().c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%zu assets)", assets != nullptr ? assets->size() : 0);
            } else {
                ImGui::TextDisabled("Assets");
            }
            ImGui::Separator();
            DrawAssets();
        }
        ImGui::EndChild();
    }

    const std::vector<ParseResultData>* SelectedAssets() {
        if (!selectedFile.has_value()) {
            return nullptr;
        }
        const auto& results = Companion::Instance->GetParseResults();
        const auto it = results.find(selectedFile.value());
        return it == results.end() ? nullptr : &it->second;
    }

    // Deduped row indices for the selected file (shared audio samples get
    // registered once per referencing bank).
    std::string rowsFile;
    std::vector<size_t> rows;

    void DrawAssets() {
        const auto* assets = SelectedAssets();
        if (assets == nullptr || assets->empty()) {
            ImGui::TextDisabled("Select a file to inspect its assets.");
            return;
        }

        if (rowsFile != selectedFile.value()) {
            rowsFile = selectedFile.value();
            rows.clear();
            std::unordered_set<std::string> seen;
            for (size_t i = 0; i < assets->size(); ++i) {
                if (seen.insert((*assets)[i].name).second) {
                    rows.push_back(i);
                }
            }
        }

        // Virtualized: lay rows out by reported height and only submit the
        // visible range. Files with thousands of assets stay responsive.
        const float sepH = ImGui::GetStyle().ItemSpacing.y * 2.0f + 1.0f;
        std::vector<float> offs(rows.size() + 1);
        float y = 0.0f;
        for (size_t i = 0; i < rows.size(); ++i) {
            offs[i] = y;
            const auto& a = (*assets)[rows[i]];
            y += (a.data.has_value() ? UIFor(a)->GetItemHeight(a) : ImGui::GetTextLineHeightWithSpacing()) + sepH;
        }
        offs[rows.size()] = y;

        const float top = ImGui::GetCursorPosY();
        const float scrollY = ImGui::GetScrollY();
        const float viewH = ImGui::GetWindowSize().y;
        size_t first = (size_t)(std::upper_bound(offs.begin(), offs.end(), scrollY - top) - offs.begin());
        if (first > 0) {
            first--;
        }
        for (size_t i = first; i < rows.size() && top + offs[i] < scrollY + viewH; ++i) {
            ImGui::SetCursorPosY(top + offs[i]);
            ImGui::PushID((int)rows[i]);
            DrawAsset((*assets)[rows[i]]);
            ImGui::PopID();
            ImGui::Separator();
        }
        ImGui::SetCursorPosY(top + offs[rows.size()]);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    BaseFactoryUI defaultUI;

    void DrawAsset(const ParseResultData& asset) {
        if (!asset.data.has_value()) {
            ImGui::Text("%s  (%s)", asset.name.c_str(), asset.type.c_str());
            return;
        }
        UIFor(asset)->DrawUI(asset);
    }

    BaseFactoryUI* UIFor(const ParseResultData& asset) {
        const auto custom = Companion::Instance->GetUIFactory(asset.type);
        return custom.has_value() ? custom.value().get() : &defaultUI;
    }


    // Build the directory tree from the full paths, stripping the directory
    // prefix common to every file so the tree starts where the paths diverge.
    void BuildTree() {
        tree = FileNode{};
        if (files.empty()) {
            return;
        }

        std::vector<std::vector<std::string>> comps;
        comps.reserve(files.size());
        size_t minLen = SIZE_MAX;
        for (const auto& f : files) {
            std::vector<std::string> parts;
            for (const auto& p : fs::path(f)) {
                const auto s = p.string();
                if (!s.empty() && s != "/") {
                    parts.push_back(s);
                }
            }
            minLen = std::min(minLen, parts.size());
            comps.push_back(std::move(parts));
        }

        // Common prefix length, capped so every file keeps at least its name.
        size_t common = minLen > 0 ? minLen - 1 : 0;
        for (size_t i = 1; i < comps.size() && common > 0; ++i) {
            size_t k = 0;
            while (k < common && comps[0][k] == comps[i][k]) {
                ++k;
            }
            common = k;
        }

        for (size_t i = 0; i < files.size(); ++i) {
            Insert(tree, comps[i], common, files[i]);
        }
    }

    static void Insert(FileNode& node, const std::vector<std::string>& parts, size_t idx, const std::string& full) {
        if (idx + 1 >= parts.size()) {
            node.files.emplace_back(parts.back(), full);
            return;
        }
        Insert(node.dirs[parts[idx]], parts, idx + 1, full);
    }

    void DrawTree(const FileNode& node) {
        const bool filtering = filter[0] != '\0';

        for (const auto& [name, child] : node.dirs) {
            if (filtering && !SubtreeMatches(child)) {
                continue;
            }
            if (filtering) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                DrawTree(child);
                ImGui::TreePop();
            }
        }

        for (const auto& [name, full] : node.files) {
            if (filtering && !ContainsCI(name, filter)) {
                continue;
            }
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selectedFile == full) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            ImGui::TreeNodeEx(full.c_str(), flags, "%s", name.c_str());
            if (ImGui::IsItemClicked()) {
                selectedFile = full;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", full.c_str());
            }
        }
    }

    bool SubtreeMatches(const FileNode& node) const {
        for (const auto& [name, full] : node.files) {
            if (ContainsCI(name, filter)) {
                return true;
            }
        }
        for (const auto& [name, child] : node.dirs) {
            if (SubtreeMatches(child)) {
                return true;
            }
        }
        return false;
    }

    static bool ContainsCI(const std::string& haystack, const std::string& needle) {
        const auto it = std::search(
            haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b) { return std::tolower(a) == std::tolower(b); });
        return it != haystack.end();
    }
};
