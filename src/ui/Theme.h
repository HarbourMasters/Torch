#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

// ImGui styling for the viewer; call once after the backend is initialised.
namespace UI {

inline void ApplyTorchTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.ChildRounding = 6.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 7);
    style.ScrollbarSize = 12.0f;

    ImVec4* colors = style.Colors;
    const ImVec4 accent = ImVec4(0.93f, 0.49f, 0.20f, 1.00f); // torch orange
    const ImVec4 accentDim = ImVec4(0.93f, 0.49f, 0.20f, 0.45f);

    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.96f);
    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.31f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_Header] = accentDim;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.93f, 0.49f, 0.20f, 0.65f);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Button] = ImVec4(0.24f, 0.24f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonActive] = accent;
    colors[ImGuiCol_SeparatorHovered] = accentDim;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.38f, 0.42f, 1.00f);
}

} // namespace UI
