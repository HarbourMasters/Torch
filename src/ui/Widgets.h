#pragma once

#ifdef BUILD_UI

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <string>

// Small shared building blocks for factory preview UIs (BaseFactoryUI::DrawUI).
namespace UI {

// Fixed body height for an asset card. The asset list precomputes each row's
// height (see MainView's manual clipper), so a card must never grow with its
// content — overflow scrolls inside this body instead.
constexpr float kAssetBodyHeight = 132.0f;

inline float AssetCardHeight() {
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y * 3 + kAssetBodyHeight;
}

// Asset name on the left, resource type on the right, then a divider.
inline void AssetHeader(const std::string& name, const std::string& type) {
    ImGui::TextUnformatted(name.c_str());
    const ImVec2 typeSize = ImGui::CalcTextSize(type.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - typeSize.x);
    ImGui::TextDisabled("%s", type.c_str());
    ImGui::Separator();
}

// Scrolling, fixed-height body region. Pair with EndAssetBody. The caller must
// have a unique ID on the ID stack (MainView pushes the asset index).
inline void BeginAssetBody() {
    ImGui::BeginChild("##assetBody", ImVec2(0, kAssetBodyHeight), false);
}

inline void EndAssetBody() {
    ImGui::EndChild();
}

// Aligned key/value line.
inline void KV(const char* label, const std::string& value) {
    ImGui::TextDisabled("%-16s", label);
    ImGui::SameLine();
    ImGui::TextUnformatted(value.c_str());
}

} // namespace UI

#endif // BUILD_UI
