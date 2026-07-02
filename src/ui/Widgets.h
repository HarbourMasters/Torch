#pragma once

#ifdef BUILD_UI

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <algorithm>
#include <map>
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
        ImGui::Separator();
        PreviewAtmosphere& atmo = GetPreviewAtmosphere();
        ImGui::Checkbox("fog", &atmo.fogEnabled);
        ImGui::ColorEdit3("fog color", atmo.fogColor, ImGuiColorEditFlags_NoInputs);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::DragIntRange2("fog range", &atmo.fogStart, &atmo.fogEnd, 2, 0, 1000);
        if (atmo.fogEnd <= atmo.fogStart) {
            atmo.fogEnd = atmo.fogStart + 1;
        }
        if (ImGui::SmallButton("reset##light")) {
            light = PreviewLighting{};
            atmo = PreviewAtmosphere{};
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

// --- resizable preview ------------------------------------------------------
// Preview canvases can be resized vertically by dragging their bottom edge.
// The width keeps the default cap (it does not grow with the height). Heights
// are persisted per asset and read back by GetItemHeight so the virtualized
// asset list allocates the right row height.

constexpr float kPreviewDefaultHeight = 320.0f;
constexpr float kPreviewMinHeight = 140.0f;
constexpr float kPreviewMaxHeight = 1400.0f;
constexpr float kPreviewGripHeight = 11.0f;

inline std::map<std::string, float>& PreviewHeights() {
    static std::map<std::string, float> heights;
    return heights;
}

inline float PreviewHeight(const std::string& key) {
    const auto it = PreviewHeights().find(key);
    if (it != PreviewHeights().end()) {
        return it->second;
    }
    if (const char* forced = std::getenv("TORCH_UI_CANVASH")) {
        const float h = (float)atof(forced);
        if (h >= kPreviewMinHeight && h <= kPreviewMaxHeight) {
            return h;
        }
    }
    return kPreviewDefaultHeight;
}

// Canvas plus grip, for GetItemHeight sizing.
inline float PreviewBlockHeight(const std::string& key) {
    return PreviewHeight(key) + kPreviewGripHeight;
}

// Preview canvas with a drag-to-resize bottom edge (double-click resets).
inline PreviewCanvas BeginResizableCanvas(const char* strId, const std::string& key, OrbitView& view) {
    const float height = PreviewHeight(key);
    const float availW = ImGui::GetContentRegionAvail().x;
    // Width limit is independent of the dragged height.
    const float vw = std::min(availW, kPreviewDefaultHeight * 3.0f);
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

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
    ImGui::PushID(strId);
    ImGui::InvisibleButton("##canvasgrip", ImVec2(vw, kPreviewGripHeight));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (active) {
        const float dy = ImGui::GetIO().MouseDelta.y;
        if (dy != 0.0f) {
            PreviewHeights()[key] = std::clamp(height + dy, kPreviewMinHeight, kPreviewMaxHeight);
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        PreviewHeights().erase(key);
    }
    if (visible) {
        // Grip bar, brighter while interacting.
        const ImVec2 mid(origin.x + vw * 0.5f, origin.y + height + kPreviewGripHeight * 0.5f);
        const ImU32 col = active    ? IM_COL32(160, 160, 170, 255)
                          : hovered ? IM_COL32(120, 120, 130, 255)
                                    : IM_COL32(70, 70, 78, 255);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mid.x - 24.0f, mid.y - 1.5f),
                                                  ImVec2(mid.x + 24.0f, mid.y + 1.5f), col, 1.5f);
    }
    return { origin, ImVec2(vw, height), visible };
}

} // namespace UI

#endif // BUILD_UI
