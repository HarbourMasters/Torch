#pragma once

#ifdef BUILD_UI

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "imgui.h"

namespace UI {

// Exports land under torch-exports/ mirroring the asset path.
inline std::filesystem::path ExportFilePath(const std::string& assetName, const char* ext) {
    std::filesystem::path path = std::filesystem::path("torch-exports") / (assetName + "." + ext);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return path;
}

inline bool WriteWavFile(const std::filesystem::path& path, const int16_t* samples, size_t frames, int channels,
                         int rate) {
    FILE* f = fopen(path.string().c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    const uint32_t dataSize = (uint32_t)(frames * channels * 2);
    const uint32_t riffSize = 36 + dataSize;
    const uint16_t fmt = 1, ch = (uint16_t)channels, bits = 16;
    const uint16_t align = (uint16_t)(channels * 2);
    const uint32_t rate32 = (uint32_t)rate, byteRate = rate32 * align, fmtSize = 16;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtSize, 4, 1, f);
    fwrite(&fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&rate32, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    fwrite(samples, 2, frames * channels, f);
    fclose(f);
    return true;
}

// Last export result per asset; shown as a "saved" marker with the full
// path in the tooltip.
inline std::unordered_map<std::string, std::string>& ExportResults() {
    static std::unordered_map<std::string, std::string> results;
    return results;
}

inline void NoteExport(const std::string& assetName, const std::string& result) {
    ExportResults()[assetName] = result;
}

inline void DrawExportMarker(const std::string& assetName) {
    const auto it = ExportResults().find(assetName);
    if (it == ExportResults().end()) {
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("saved");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", it->second.c_str());
    }
}

} // namespace UI

#endif // BUILD_UI
