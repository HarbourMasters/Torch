#ifdef BUILD_UI

#include "SequenceDriver.h"

#include <algorithm>
#include <unordered_map>

#include "ui/BaseBackend.h"
#include "ui/Widgets.h"

namespace UI {
namespace {

RenderedAudio sRendered;
std::string sRenderedName;
int sRenderedOption = -1;
std::string sPlayingName;
std::unordered_map<std::string, int> sOptionChoice;

bool RenderCached(SequenceDriver* driver, const ParseResultData& item, int option) {
    if (sRenderedName == item.name && sRenderedOption == option) {
        return !sRendered.pcm.empty();
    }
    sRendered = {};
    sRenderedName = item.name;
    sRenderedOption = option;
    if (!driver->Render(item, option, sRendered)) {
        sRendered = {};
        return false;
    }
    sRendered.seconds = (double)sRendered.pcm.size() / 2.0 / kSynthRate;
    return !sRendered.pcm.empty();
}

} // namespace

float SequencePreviewUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetFrameHeightWithSpacing() * 2.0f +
           ImGui::GetStyle().ItemSpacing.y * 4.0f;
}

void SequencePreviewUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    SequenceDriver* driver = GetSequenceDriver(item.type);
    if (driver == nullptr || !item.data.has_value()) {
        ImGui::TextDisabled(driver == nullptr ? "no sequence driver for this type" : "no data");
        return;
    }
    ImGui::TextDisabled("sequence");

    const std::vector<std::string> options = driver->Options(item);
    auto [choiceIt, inserted] = sOptionChoice.try_emplace(item.name, 0);
    if (inserted) {
        choiceIt->second = driver->DefaultOption(item);
    }
    int& choice = choiceIt->second;
    if (!options.empty()) {
        choice = std::clamp(choice, 0, (int)options.size() - 1);
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * 0.4f, 260.0f));
        if (ImGui::BeginCombo("##seqopt", options[choice].c_str())) {
            for (int i = 0; i < (int)options.size(); ++i) {
                if (ImGui::Selectable(options[i].c_str(), i == choice)) {
                    choice = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }

    const bool playingThis = sPlayingName == item.name && GetBackend()->AudioProgress() >= 0.0f;
    if (ImGui::Button(playingThis ? "Stop##seq" : "Play##seq")) {
        if (playingThis) {
            GetBackend()->StopAudio();
            sPlayingName.clear();
        } else if (RenderCached(driver, item, choice)) {
            GetBackend()->SetAudioSpeed(1.0f);
            if (GetBackend()->PlaySamples(sRendered.pcm.data(), sRendered.pcm.size() / 2, kSynthRate, 2)) {
                sPlayingName = item.name;
            }
        }
    }
    ImGui::SameLine();
    float volume = GetBackend()->GetAudioVolume();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderFloat("##seqvol", &volume, 0.0f, 1.0f, "vol %.2f")) {
        GetBackend()->SetAudioVolume(volume);
    }
    ImGui::SameLine();
    if (sRenderedName == item.name && !sRendered.pcm.empty()) {
        ImGui::TextDisabled("%zu notes, %.1fs", sRendered.noteCount, sRendered.seconds);
    } else {
        ImGui::TextDisabled("press play to render");
    }

    float progress = playingThis ? std::max(GetBackend()->AudioProgress(), 0.0f) : 0.0f;
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 420.0f));
    if (ImGui::SliderFloat("##seqseek", &progress, 0.0f, 1.0f, "") && playingThis) {
        GetBackend()->SeekAudio(progress);
    }
}

} // namespace UI

#endif // BUILD_UI
