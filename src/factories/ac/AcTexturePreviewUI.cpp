#include "AcTexturePreviewUI.h"

#ifdef BUILD_UI

#include "BtiTextureFactory.h"
#include "Companion.h"
#include "ItemBillboardTextureFactory.h"
#include "PlayerClothTextureFactory.h"
#include "ui/BaseBackend.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>

namespace AC {
namespace {

struct PreviewTexture {
    const std::vector<uint8_t>* rgba;
    const std::string* archivePath;
    uint16_t width;
    uint16_t height;
};

std::optional<PreviewTexture> GetPreviewTexture(const ParseResultData& item) {
    if (!item.data.has_value()) {
        return std::nullopt;
    }
    if (item.type == "AC:BTI_TEXTURE") {
        const auto data = std::static_pointer_cast<BtiTextureData>(item.data.value());
        return PreviewTexture{ &data->rgba, &data->archivePath, data->width, data->height };
    }
    if (item.type == "AC:PLAYER_CLOTH_TEXTURE") {
        const auto data = std::static_pointer_cast<PlayerClothTextureData>(item.data.value());
        return PreviewTexture{ &data->rgba, &data->archivePath, 32, 32 };
    }
    if (item.type == "AC:ITEM_BILLBOARD_TEXTURE") {
        const auto data = std::static_pointer_cast<ItemBillboardTextureData>(item.data.value());
        return PreviewTexture{ &data->rgba, &data->archivePath, data->width, data->height };
    }
    return std::nullopt;
}

UI::TextureHandle GetOrLoadTexture(const ParseResultData& item, const PreviewTexture& preview) {
    static std::unordered_map<std::string, UI::TextureHandle> textureCache;
    const std::string key = item.type + "\n" + *preview.archivePath;
    if (const auto cached = textureCache.find(key); cached != textureCache.end()) {
        return cached->second;
    }

    UI::TextureHandle handle = UI::kInvalidTexture;
    const size_t expectedSize = static_cast<size_t>(preview.width) * preview.height * 4U;
    if (UI::GetBackend() != nullptr && preview.width > 0 && preview.height > 0 &&
        preview.rgba->size() == expectedSize) {
        handle = UI::GetBackend()->UploadRGBA8(preview.rgba->data(), preview.width, preview.height);
    }
    textureCache.emplace(key, handle);
    return handle;
}

void DrawCheckerboard(ImDrawList* drawList, const ImVec2& origin, const ImVec2& size) {
    constexpr float cellSize = 8.0f;
    drawList->AddRectFilled(origin, origin + size, IM_COL32(48, 48, 52, 255));
    for (float y = 0; y < size.y; y += cellSize) {
        for (float x = 0; x < size.x; x += cellSize) {
            if ((static_cast<int>((x + y) / cellSize) % 2) == 0) {
                continue;
            }
            const ImVec2 cellOrigin = origin + ImVec2(x, y);
            const ImVec2 cellSizeClipped(std::min(cellSize, size.x - x), std::min(cellSize, size.y - y));
            drawList->AddRectFilled(cellOrigin, cellOrigin + cellSizeClipped, IM_COL32(64, 64, 70, 255));
        }
    }
}

ImVec2 FitTexture(uint16_t width, uint16_t height, float extent) {
    if (width == 0 || height == 0) {
        return ImVec2(0.0f, 0.0f);
    }
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    return aspect >= 1.0f ? ImVec2(extent, extent / aspect) : ImVec2(extent * aspect, extent);
}

} // namespace

float TexturePreviewUI::GetItemHeight(const ParseResultData& /*item*/) {
    return 138.0f;
}

void TexturePreviewUI::DrawUI(const ParseResultData& item) {
    constexpr float thumbnailExtent = 128.0f;
    const auto preview = GetPreviewTexture(item);
    if (!preview.has_value()) {
        BaseFactoryUI::DrawUI(item);
        return;
    }

    const ImVec2 thumbnail(thumbnailExtent, thumbnailExtent);
    const UI::TextureHandle handle = GetOrLoadTexture(item, preview.value());
    ImGui::PushID(item.name.c_str());
    ImGui::BeginGroup();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    DrawCheckerboard(drawList, origin, thumbnail);
    if (handle != UI::kInvalidTexture) {
        const ImVec2 fitted = FitTexture(preview->width, preview->height, thumbnailExtent);
        const ImVec2 padding((thumbnail.x - fitted.x) * 0.5f, (thumbnail.y - fitted.y) * 0.5f);
        ImGui::SetCursorScreenPos(origin + padding);
        ImGui::Image(handle, fitted);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Image(handle, FitTexture(preview->width, preview->height, 256.0f));
            ImGui::Text("%u x %u RGBA8", preview->width, preview->height);
            ImGui::EndTooltip();
        }
    } else {
        const char* label = "No Preview";
        const ImVec2 center = origin + thumbnail * 0.5f;
        drawList->AddText(center - ImGui::CalcTextSize(label) * 0.5f, IM_COL32(150, 150, 150, 255), label);
    }
    drawList->AddRect(origin, origin + thumbnail, IM_COL32(255, 255, 255, 40), 4.0f);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(thumbnail);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(item.name.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted(item.type.c_str());
    ImGui::Text("Resolution  %u x %u", preview->width, preview->height);
    ImGui::Text("Pixels      %zu bytes", preview->rgba->size());
    ImGui::Spacing();
    if (ImGui::SmallButton("Copy archive path")) {
        ImGui::SetClipboardText(preview->archivePath->c_str());
    }
    ImGui::TextWrapped("%s", preview->archivePath->c_str());
    ImGui::EndGroup();
    ImGui::PopID();
}

} // namespace AC

#endif
