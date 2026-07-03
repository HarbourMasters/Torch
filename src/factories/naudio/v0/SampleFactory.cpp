#include "SampleFactory.h"

#include <vector>
#include "Companion.h"
#include "AIFCDecode.h"
#include "spdlog/spdlog.h"
#include <factories/naudio/v1/AudioConverter.h>

ExportResult SampleModdingExporter::Export(std::ostream& writer, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto sample = std::static_pointer_cast<SampleData>(raw);
    *replacement += ".aiff";

    LUS::BinaryWriter aifc = LUS::BinaryWriter();
    AudioConverter::SampleV0ToAIFC(&sample->mSample, aifc);

    LUS::BinaryWriter aiff = LUS::BinaryWriter();
    write_aiff(aifc.ToVector(), aiff);
    aifc.Close();
    aiff.Finish(writer);
    return std::nullopt;
}

ExportResult SampleBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                          YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto sample = std::static_pointer_cast<SampleData>(raw)->mSample;

    WriteHeader(writer, Torch::ResourceType::Sample, 0);
    writer.Write(sample.loop.start);
    writer.Write(sample.loop.end);
    writer.Write(sample.loop.count);
    writer.Write(sample.loop.pad);

    if (sample.loop.state.has_value()) {
        auto state = sample.loop.state.value();
        writer.Write(static_cast<uint32_t>(state.size()));
        writer.Write(reinterpret_cast<char*>(state.data()), state.size() * sizeof(int16_t));
    } else {
        writer.Write(static_cast<uint32_t>(0));
    }

    writer.Write(sample.book.order);
    writer.Write(sample.book.npredictors);

    writer.Write(static_cast<uint32_t>(sample.book.table.size()));
    writer.Write(reinterpret_cast<char*>(sample.book.table.data()), sample.book.table.size() * sizeof(int16_t));

    writer.Write(static_cast<int32_t>(sample.data.size()));
    writer.Write(reinterpret_cast<char*>(sample.data.data()), sample.data.size());

    writer.Write(sample.name);

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SampleFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    const auto id = data["id"].as<int32_t>();
    if (AudioManager::Instance == nullptr) {
        throw std::runtime_error("AudioManager not initialized");
    }
    AudioBankSample entry = AudioManager::Instance->get_aifc(id);
    return std::make_shared<SampleData>(entry);
}
#ifdef BUILD_UI
#include <cmath>
#include <cstring>

#include "ui/BaseBackend.h"
#include "ui/ExportUtils.h"
#include "ui/Widgets.h"

namespace {

struct DecodedSample {
    std::string name;
    std::vector<int16_t> pcm;
    int rate = 0;
    int channels = 1;
};
DecodedSample sDecoded; // last decoded sample (they can be large)
std::string sPlayingName;
std::unordered_map<std::string, float> sSampleSpeeds;

uint32_t ReadU32BE(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// 80-bit IEEE 754 extended float, as used by the AIFF COMM sample rate.
double ReadExtended80(const uint8_t* p) {
    const int exponent = (((p[0] & 0x7F) << 8) | p[1]) - 16383;
    uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i) {
        mantissa = (mantissa << 8) | p[2 + i];
    }
    double value = (double)mantissa * std::ldexp(1.0, exponent - 63);
    return (p[0] & 0x80) != 0 ? -value : value;
}

bool ParseAiff(const std::vector<char>& bytes, DecodedSample& out) {
    const auto* data = (const uint8_t*)bytes.data();
    const size_t size = bytes.size();
    if (size < 12 || std::memcmp(data, "FORM", 4) != 0 || std::memcmp(data + 8, "AIFF", 4) != 0) {
        return false;
    }
    uint32_t frames = 0;
    int sampleBits = 16;
    size_t pos = 12;
    bool gotCommon = false, gotData = false;
    while (pos + 8 <= size) {
        const uint32_t ckSize = ReadU32BE(data + pos + 4);
        const uint8_t* ck = data + pos + 8;
        // The AIFC decoder over-declares the SSND size (the header overwrites
        // the head of the sample data), so clamp instead of rejecting.
        const size_t avail = std::min((size_t)ckSize, size - pos - 8);
        if (std::memcmp(data + pos, "COMM", 4) == 0 && avail >= 18) {
            out.channels = (int16_t)((ck[0] << 8) | ck[1]);
            frames = ReadU32BE(ck + 2);
            sampleBits = (int16_t)((ck[6] << 8) | ck[7]);
            out.rate = (int)std::lround(ReadExtended80(ck + 8));
            gotCommon = true;
        } else if (std::memcmp(data + pos, "SSND", 4) == 0 && avail >= 8) {
            const uint32_t offset = ReadU32BE(ck);
            if (avail > 8 + offset) {
                const size_t count = (avail - 8 - offset) / 2;
                out.pcm.resize(count);
                const uint8_t* smp = ck + 8 + offset;
                for (size_t i = 0; i < count; ++i) {
                    out.pcm[i] = (int16_t)((smp[i * 2] << 8) | smp[i * 2 + 1]);
                }
                gotData = true;
            }
        }
        pos += 8 + ckSize + (ckSize & 1);
    }
    (void)frames;
    return gotCommon && gotData && sampleBits == 16 && out.rate > 0 && out.channels > 0 && !out.pcm.empty();
}

bool DecodeSample(const ParseResultData& item) {
    if (sDecoded.name == item.name) {
        return !sDecoded.pcm.empty();
    }
    sDecoded = {};
    sDecoded.name = item.name;

    auto sample = std::static_pointer_cast<SampleData>(item.data.value());
    const bool ok = DecodeSampleToPcm(&sample->mSample, sDecoded.pcm, sDecoded.rate);
    sDecoded.channels = 1;
    return ok;
}

} // namespace

bool DecodeAiffBytes(const std::vector<char>& bytes, std::vector<int16_t>& pcm, int& rate) {
    DecodedSample decoded;
    if (!ParseAiff(bytes, decoded)) {
        return false;
    }
    pcm = std::move(decoded.pcm);
    rate = decoded.rate;
    return true;
}

bool DecodeSampleToPcm(AudioBankSample* sample, std::vector<int16_t>& pcm, int& rate) {
    DecodedSample decoded;
    try {
        LUS::BinaryWriter aifc;
        AudioConverter::SampleV0ToAIFC(sample, aifc);
        LUS::BinaryWriter aiff;
        // write_aiff throws std::runtime_error on malformed/truncated sample data.
        write_aiff(aifc.ToVector(), aiff);
        aifc.Close();

        const bool ok = ParseAiff(aiff.ToVector(), decoded);
        aiff.Close();
        if (!ok) {
            return false;
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to decode audio sample: {}", e.what());
        return false;
    }
    // The converter's header overwrites the head of the decoded stream; pad the
    // front back to the expected frame count so loop points stay aligned.
    const size_t expected = sample->data.size() * 16 / 9;
    if (decoded.pcm.size() < expected) {
        decoded.pcm.insert(decoded.pcm.begin(), expected - decoded.pcm.size(), 0);
    }
    // Tunings are stored as floats, so derived rates land near the standard
    // recording rates; snap when within 6%.
    static const int kRates[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
    for (const int snap : kRates) {
        if (std::fabs((float)decoded.rate - (float)snap) <= snap * 0.06f) {
            decoded.rate = snap;
            break;
        }
    }
    pcm = std::move(decoded.pcm);
    rate = decoded.rate;
    return true;
}

float SampleFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetFrameHeightWithSpacing() * 2.0f +
           ImGui::GetStyle().ItemSpacing.y * 4.0f;
}

void SampleFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto& sample = std::static_pointer_cast<SampleData>(item.data.value())->mSample;
    ImGui::TextDisabled("sample  \xe2\x80\x94  %zu bytes vadpcm, loop %u-%u (x%d), book order %d", sample.data.size(),
                        sample.loop.start, sample.loop.end, sample.loop.count, sample.book.order);

    auto speedIt = sSampleSpeeds.emplace(item.name, 1.0f).first;
    const bool playingThis = sPlayingName == item.name && UI::GetBackend()->AudioProgress() >= 0.0f;
    if (ImGui::Button(playingThis ? "Stop##sample" : "Play##sample")) {
        if (playingThis) {
            UI::GetBackend()->StopAudio();
            sPlayingName.clear();
        } else if (DecodeSample(item)) {
            if (UI::GetBackend()->PlaySamples(sDecoded.pcm.data(), sDecoded.pcm.size() / sDecoded.channels,
                                              sDecoded.rate, sDecoded.channels)) {
                sPlayingName = item.name;
                UI::GetBackend()->SetAudioSpeed(speedIt->second);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("WAV##sampleexp")) {
        if (DecodeSample(item)) {
            const auto path = UI::ExportFilePath(item.name, "wav");
            UI::NoteExport(item.name,
                           UI::WriteWavFile(path, sDecoded.pcm.data(), sDecoded.pcm.size() / sDecoded.channels,
                                            sDecoded.channels, sDecoded.rate)
                               ? path.string()
                               : "export failed");
        } else {
            UI::NoteExport(item.name, "decode failed");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Export decoded sample to torch-exports/");
    }
    UI::DrawExportMarker(item.name);
    ImGui::SameLine();
    float volume = UI::GetBackend()->GetAudioVolume();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderFloat("##vol", &volume, 0.0f, 1.0f, "vol %.2f")) {
        UI::GetBackend()->SetAudioVolume(volume);
    }
    ImGui::SameLine();
    float& speed = speedIt->second;
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderFloat("##speed", &speed, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
        // Snap near the common half/normal/double rates.
        static const float kSnaps[] = { 0.5f, 1.0f, 2.0f };
        for (const float snap : kSnaps) {
            if (std::fabs(speed - snap) < 0.05f) {
                speed = snap;
                break;
            }
        }
        if (playingThis) {
            UI::GetBackend()->SetAudioSpeed(speed);
        }
    }
    ImGui::SameLine();
    if (sDecoded.name == item.name && !sDecoded.pcm.empty()) {
        const float seconds = (float)sDecoded.pcm.size() / (float)(sDecoded.rate * sDecoded.channels);
        ImGui::TextDisabled("%d Hz, %s, %.2fs", sDecoded.rate, sDecoded.channels == 1 ? "mono" : "stereo", seconds);
    } else {
        ImGui::TextDisabled("press play to decode");
    }

    float progress = playingThis ? std::max(UI::GetBackend()->AudioProgress(), 0.0f) : 0.0f;
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 420.0f));
    if (ImGui::SliderFloat("##seek", &progress, 0.0f, 1.0f, "") && playingThis) {
        UI::GetBackend()->SeekAudio(progress);
    }
}
#endif // BUILD_UI
