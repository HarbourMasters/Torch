#pragma once

#ifdef BUILD_UI

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <algorithm>
#include <string>

#include "ui/BaseBackend.h"

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

// Button + popup editing the shared preview light.
inline void LightingControls() {
    if (ImGui::SmallButton("light")) {
        ImGui::OpenPopup("##previewlight");
    }
    if (ImGui::BeginPopup("##previewlight")) {
        PreviewLighting& light = GetPreviewLighting();
        ImGui::Checkbox("enabled", &light.enabled);
        ImGui::ColorEdit3("ambient", light.ambient, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit3("color", light.color, ImGuiColorEditFlags_NoInputs);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat3("position", light.position, -4.0f, 4.0f, "%.2f");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat("intensity", &light.intensity, 0.0f, 3.0f, "%.2f");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat("falloff", &light.falloff, 0.0f, 2.0f, "%.2f");
        if (ImGui::SmallButton("reset##light")) {
            light = PreviewLighting{};
        }
        ImGui::EndPopup();
    }
}

// Camera controls for the item submitted just before this call: drag orbits,
// shift+drag or middle-drag pans, cmd/ctrl+scroll zooms, double-click resets.
inline void OrbitControls(OrbitView& view) {
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();

    const bool panning = (ImGui::IsItemActive() && io.KeyShift) ||
                         (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle));
    if (panning) {
        view.panX += io.MouseDelta.x;
        view.panY += io.MouseDelta.y;
    } else if (ImGui::IsItemActive()) {
        view.yaw -= io.MouseDelta.x * 0.01f;
        view.pitch = std::clamp(view.pitch + io.MouseDelta.y * 0.01f, -1.55f, 1.55f);
    }
    // Zoom needs a modifier so plain scroll keeps scrolling the asset list.
    if (hovered && io.MouseWheel != 0.0f && (io.KeyCtrl || io.KeySuper)) {
        view.zoom = std::clamp(view.zoom * (1.0f + io.MouseWheel * 0.1f), 0.02f, 50.0f);
        io.MouseWheel = 0.0f;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        view = OrbitView{};
    }
}

// Centered preview box (aspect capped at 3:1) with orbit controls and a dark
// backdrop. `visible` is an on-screen test against the scroll viewport.
struct PreviewCanvas {
    ImVec2 origin;
    ImVec2 size;
    bool visible;
};

inline PreviewCanvas BeginPreviewCanvas(const char* strId, float height, OrbitView& view) {
    const float availW = ImGui::GetContentRegionAvail().x;
    const float vw = std::min(availW, height * 3.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - vw) * 0.5f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(strId, ImVec2(vw, height));
    const float winTop = ImGui::GetWindowPos().y;
    const float winBot = winTop + ImGui::GetWindowHeight();
    const bool visible = ImGui::GetItemRectMax().y > winTop && ImGui::GetItemRectMin().y < winBot;
    OrbitControls(view);
    if (visible) {
        ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + vw, origin.y + height),
                                                  IM_COL32(18, 18, 22, 255));
    }
    return { origin, ImVec2(vw, height), visible };
}

} // namespace UI

#endif // BUILD_UI
