#include "AudioPreview.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Companion.h"
#include "spdlog/spdlog.h"

// PM64 audio preview, ported from pmret (src/audio/bgm_player.c,
// core/engine.c, core/voice.c). The SBN container indexes BGM songs, BK
// instrument banks and the INIT tables binding songs to banks.

namespace PM64Audio {
namespace {

uint32_t RD32(const std::vector<uint8_t>& d, uint64_t off) {
    if (off + 4 > d.size()) {
        return 0;
    }
    return ((uint32_t)d[off] << 24) | ((uint32_t)d[off + 1] << 16) | ((uint32_t)d[off + 2] << 8) | d[off + 3];
}

uint16_t RD16(const std::vector<uint8_t>& d, uint64_t off) {
    if (off + 2 > d.size()) {
        return 0;
    }
    return (uint16_t)((d[off] << 8) | d[off + 1]);
}

uint8_t RD8(const std::vector<uint8_t>& d, uint64_t off) {
    return off < d.size() ? d[off] : 0;
}

std::string RdName(const std::vector<uint8_t>& d, uint64_t off) {
    std::string out;
    for (int i = 0; i < 4; ++i) {
        const char c = (char)RD8(d, off + i);
        if (c > 0x20 && c < 0x7F) {
            out.push_back(c);
        }
    }
    return out.empty() ? "____" : out;
}

// Copies an envelope command list (interval/value pairs) up to and including
// its ENV_CMD_END terminator.
std::vector<uint8_t> CopyEnvList(const std::vector<uint8_t>& d, uint64_t off) {
    std::vector<uint8_t> out;
    for (int i = 0; i < 64; ++i) {
        const uint8_t a = RD8(d, off + i * 2);
        const uint8_t b = RD8(d, off + i * 2 + 1);
        out.push_back(a);
        out.push_back(b);
        if (a == 0xFF) { // ENV_CMD_END
            break;
        }
    }
    return out;
}

bool sPreviewAssets = false;

} // namespace

std::map<uint32_t, std::shared_ptr<Bank>>& BankRegistry() {
    static std::map<uint32_t, std::shared_ptr<Bank>> registry;
    return registry;
}

GlobalData& Globals() {
    static GlobalData data;
    return data;
}

void SetPreviewAssets(bool enabled) {
    sPreviewAssets = enabled;
}

bool PreviewAssets() {
    return sPreviewAssets;
}

std::shared_ptr<Bank> ParseBank(const std::vector<uint8_t>& rom, uint32_t off) {
    auto& registry = BankRegistry();
    const auto it = registry.find(off);
    if (it != registry.end()) {
        return it->second;
    }
    auto bank = std::make_shared<Bank>();
    if (RD16(rom, off) != 0x424B || RD16(rom, off + 0x0C) != 0x4352) { // 'BK', format 'CR'
        registry[off] = bank;
        return bank;
    }
    const std::string name = RdName(rom, off + 8);
    std::memcpy(bank->name, name.c_str(), std::min<size_t>(4, name.size()));

    for (int slot = 0; slot < 16; ++slot) {
        const uint16_t insOff = RD16(rom, off + 0x12 + slot * 2);
        if (insOff == 0) {
            continue;
        }
        const uint64_t p = (uint64_t)off + insOff;
        auto ins = std::make_shared<Instrument>();
        const uint32_t wavOff = RD32(rom, p);
        const uint32_t wavLen = RD32(rom, p + 4);
        ins->loopStart = (int32_t)RD32(rom, p + 0x0C);
        ins->loopEnd = (int32_t)RD32(rom, p + 0x10);
        ins->loopCount = (int32_t)RD32(rom, p + 0x14);
        const uint32_t predOff = RD32(rom, p + 0x18);
        const uint16_t bookSize = RD16(rom, p + 0x1C);
        ins->keyBase = RD16(rom, p + 0x1E);
        ins->sampleRate = (int32_t)RD32(rom, p + 0x20);
        ins->type = RD8(rom, p + 0x24);
        const uint32_t envOff = RD32(rom, p + 0x2C);

        if (wavLen == 0 || wavLen > 0x400000 || (uint64_t)off + wavOff + wavLen > rom.size()) {
            continue;
        }
        ins->wav.assign(rom.begin() + off + wavOff, rom.begin() + off + wavOff + wavLen);
        const int bookCount = bookSize / 2;
        ins->book.reserve(bookCount);
        for (int i = 0; i < bookCount; ++i) {
            ins->book.push_back((int16_t)RD16(rom, (uint64_t)off + predOff + i * 2));
        }
        if (envOff != 0) {
            const uint64_t env = (uint64_t)off + envOff;
            const uint8_t count = RD8(rom, env);
            for (int e = 0; e < count && e < 4; ++e) {
                const uint16_t press = RD16(rom, env + 4 + e * 4);
                const uint16_t release = RD16(rom, env + 4 + e * 4 + 2);
                ins->envelopes.emplace_back(CopyEnvList(rom, env + press), CopyEnvList(rom, env + release));
            }
        }
        bank->instruments[slot] = std::move(ins);
    }
    registry[off] = bank;
    return bank;
}

void RegisterPreviewAssets(const std::vector<uint8_t>& rom, uint32_t sbnBase) {
    if (RD32(rom, sbnBase) != 0x53424E20) { // 'SBN '
        return;
    }
    const uint32_t fileListOff = RD32(rom, sbnBase + 0x10);
    const uint32_t numEntries = RD32(rom, sbnBase + 0x14);
    const uint32_t initOff = RD32(rom, sbnBase + 0x24);
    if (numEntries == 0 || numEntries > 4096 || initOff == 0) {
        return;
    }

    struct FileEntry {
        uint32_t romOff = 0;
        uint32_t size = 0;
        uint8_t fmt = 0;
    };
    std::vector<FileEntry> files(numEntries);
    for (uint32_t i = 0; i < numEntries; ++i) {
        const uint64_t e = (uint64_t)sbnBase + fileListOff + i * 8;
        const uint32_t data = RD32(rom, e + 4);
        files[i].romOff = sbnBase + (RD32(rom, e) & 0xFFFFFF);
        files[i].size = data & 0xFFFFFF;
        files[i].fmt = data >> 24;
    }

    // Register a sample card per bank instrument (AU_FMT_BK = 0x30).
    for (uint32_t i = 0; i < numEntries; ++i) {
        if (files[i].fmt != 0x30) {
            continue;
        }
        auto bank = ParseBank(rom, files[i].romOff);
        char base[64];
        snprintf(base, sizeof(base), "banks/%02X_%s", i, bank->name);
        for (int slot = 0; slot < 16; ++slot) {
            if (bank->instruments[slot] == nullptr) {
                continue;
            }
            YAML::Node node;
            node["type"] = "PM64:BK_SAMPLE";
            node["offset"] = files[i].romOff;
            node["instrument"] = slot;
            node["autogen"] = true;
            char name[80];
            snprintf(name, sizeof(name), "%s/%02d", base, slot);
            Companion::Instance->RegisterAsset(name, node);
        }
    }

    // INIT tables: global bank assignments, songs, and the extra file list
    // ([0] SEF, [1] PER, [2] PRG).
    const uint64_t init = (uint64_t)sbnBase + initOff;
    const uint16_t bankListOff = RD16(rom, init + 0x08);
    const uint16_t songListOff = RD16(rom, init + 0x0C);
    const uint16_t songListSize = RD16(rom, init + 0x0E);
    const uint16_t extraListOff = RD16(rom, init + 0x10);

    // Global banks: (bankSet, bankIndex) -> BK rom offset. BankSet values map
    // onto BankSetIndex slots: AUX(1)->0 and 7, 2->1, MUSIC(3)->3, 4..6 as-is.
    std::vector<std::pair<uint32_t, uint32_t>> globalBanks; // (setIdx<<8|bankIdx, romOff)
    for (int i = 0; i < 80; ++i) {
        const uint64_t e = init + bankListOff + i * 4;
        const uint16_t fileIndex = RD16(rom, e);
        if (fileIndex == 0xFFFF) {
            break;
        }
        const uint8_t bankIndex = RD8(rom, e + 2);
        const uint8_t bankSet = RD8(rom, e + 3);
        if (fileIndex >= numEntries) {
            continue;
        }
        const uint32_t romOff = files[fileIndex].romOff;
        ParseBank(rom, romOff);
        switch (bankSet) {
            case 1:
                globalBanks.emplace_back((0u << 8) | bankIndex, romOff);
                globalBanks.emplace_back((7u << 8) | bankIndex, romOff);
                break;
            case 2:
                globalBanks.emplace_back((1u << 8) | bankIndex, romOff);
                break;
            case 3:
                globalBanks.emplace_back((3u << 8) | bankIndex, romOff);
                break;
            case 4:
            case 5:
            case 6:
                globalBanks.emplace_back(((uint32_t)bankSet << 8) | bankIndex, romOff);
                break;
            default:
                break;
        }
    }

    // PER drums and PRG programs.
    Globals().perDrums.clear();
    Globals().prgPrograms.clear();
    const uint16_t perIndex = RD16(rom, init + extraListOff + 2);
    const uint16_t prgIndex = RD16(rom, init + extraListOff + 4);
    if (perIndex < numEntries && files[perIndex].size > 0x10) {
        const uint64_t per = files[perIndex].romOff + 0x10;
        const uint32_t count = std::min<uint32_t>((files[perIndex].size - 0x10) / 0x0C, 72);
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t p = per + i * 0x0C;
            DrumInfo d;
            d.bankPatch = RD16(rom, p);
            d.keyBase = RD16(rom, p + 2);
            d.volume = RD8(rom, p + 4);
            d.pan = (int8_t)RD8(rom, p + 5);
            d.reverb = RD8(rom, p + 6);
            d.randTune = RD8(rom, p + 7);
            d.randVolume = RD8(rom, p + 8);
            d.randPan = RD8(rom, p + 9);
            d.randReverb = RD8(rom, p + 10);
            Globals().perDrums.push_back(d);
        }
    }
    if (prgIndex < numEntries && files[prgIndex].size > 0x10) {
        const uint64_t prg = files[prgIndex].romOff + 0x10;
        const uint32_t count = std::min<uint32_t>((files[prgIndex].size - 0x10) / 8, 64);
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t p = prg + i * 8;
            ProgramInfo pr;
            pr.bankPatch = RD16(rom, p);
            pr.volume = RD8(rom, p + 2);
            pr.pan = (int8_t)RD8(rom, p + 3);
            pr.reverb = RD8(rom, p + 4);
            pr.coarseTune = (int8_t)RD8(rom, p + 5);
            pr.fineTune = (int8_t)RD8(rom, p + 6);
            Globals().prgPrograms.push_back(pr);
        }
    }

    // Songs: one PM64:BGM asset each, carrying its bank bindings (global
    // banks plus the song's aux BK files loaded into AUX slots 0-2).
    const uint32_t songCount = songListSize / 8 > 0 ? songListSize / 8 - 1 : 0;
    for (uint32_t i = 0; i < songCount; ++i) {
        const uint64_t e = init + songListOff + i * 8;
        const uint16_t bgmIndex = RD16(rom, e);
        if (bgmIndex == 0 || bgmIndex >= numEntries || files[bgmIndex].fmt != 0x10) {
            continue;
        }
        YAML::Node node;
        node["type"] = "PM64:BGM";
        node["offset"] = files[bgmIndex].romOff;
        node["size"] = files[bgmIndex].size;
        node["autogen"] = true;
        for (const auto& [key, romOff] : globalBanks) {
            node["banks"].push_back(key);
            node["banks"].push_back(romOff);
        }
        for (int b = 0; b < 3; ++b) {
            const uint16_t bkIndex = RD16(rom, e + 2 + b * 2);
            if (bkIndex != 0 && bkIndex < numEntries && files[bkIndex].fmt == 0x30) {
                ParseBank(rom, files[bkIndex].romOff);
                node["banks"].push_back((0u << 8) | b);
                node["banks"].push_back(files[bkIndex].romOff);
                node["banks"].push_back((7u << 8) | b);
                node["banks"].push_back(files[bkIndex].romOff);
            }
        }
        char name[64];
        snprintf(name, sizeof(name), "bgm/%03d_%s", i, RdName(rom, files[bgmIndex].romOff + 8).c_str());
        Companion::Instance->RegisterAsset(name, node);
    }
    SPDLOG_INFO("PM64 audio preview: {} SBN files, {} songs, {} global banks, {} drums, {} programs", numEntries,
                songCount, globalBanks.size(), Globals().perDrums.size(), Globals().prgPrograms.size());
}

} // namespace PM64Audio

std::optional<std::shared_ptr<IParsedData>> PM64BkSampleFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto data = std::make_shared<PM64BkSampleData>();
    data->bank = PM64Audio::ParseBank(buffer, GetSafeNode<uint32_t>(node, "offset"));
    data->slot = GetSafeNode<int>(node, "instrument");
    if (data->bank == nullptr || data->slot < 0 || data->slot >= 16 || data->bank->instruments[data->slot] == nullptr) {
        return std::nullopt;
    }
    return data;
}

ExportResult PM64BkSampleBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto data = std::static_pointer_cast<PM64BkSampleData>(raw);
    const auto& ins = data->bank->instruments[data->slot];
    if (ins != nullptr) {
        write.write((const char*)ins->wav.data(), ins->wav.size());
    }
    return std::nullopt;
}

ExportResult PM64BgmBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto data = std::static_pointer_cast<PM64BgmData>(raw);
    write.write((const char*)data->file.data(), data->file.size());
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> PM64BgmFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const uint32_t offset = GetSafeNode<uint32_t>(node, "offset");
    const uint32_t size = GetSafeNode<uint32_t>(node, "size");
    if (size < 0x24 || (uint64_t)offset + size > buffer.size()) {
        return std::nullopt;
    }
    auto data = std::make_shared<PM64BgmData>();
    data->file.assign(buffer.begin() + offset, buffer.begin() + offset + size);
    return data;
}

// --- preview UI + BGM driver -------------------------------------------------
#ifdef BUILD_UI

#include <map>
#include <unordered_map>
#include "imgui.h"
#include "ui/BaseBackend.h"
#include "ui/ExportUtils.h"
#include "ui/Widgets.h"
#include "ui/audio/SequenceSynth.h"

namespace {

// pmret AuEnvelopeIntervals: envelope command intervals in microseconds.
// Indices 0-79 are the seconds table; 80-93 are N x 5750us audio frames.
double EnvIntervalSec(uint8_t idx) {
    static const double kSeconds[80] = {
        60,   55,   50,   45,   40,    35,   30,    27.5, 25,    22.5, 20,   19,   18,   17,   16,   15,
        14,   13,   12,   11,   10,    9,    8,     7,    6,     5,    4.5,  4,    3.5,  3,    2.75, 2.5,
        2.25, 2,    1.9,  1.8,  1.7,   1.6,  1.5,   1.4,  1.3,   1.2,  1.1,  1,    0.95, 0.9,  0.85, 0.8,
        0.75, 0.7,  0.65, 0.6,  0.55,  0.5,  0.45,  0.4,  0.375, 0.35, 0.325, 0.3, 0.29, 0.28, 0.27, 0.26,
        0.25, 0.24, 0.23, 0.22, 0.21,  0.2,  0.19,  0.18, 0.17,  0.16, 0.15, 0.14, 0.13, 0.12, 0.11, 0.1,
    };
    static const int kUnits[14] = { 16, 14, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 };
    if (idx < 80) {
        return kSeconds[idx];
    }
    if (idx < 94) {
        return kUnits[idx - 80] * 0.00575;
    }
    return 0.0;
}

// VADPCM (order 2) decode. Decoding linearly from the start reproduces the
// loop-point state, so the stored ADPCM_STATE is not needed.
std::shared_ptr<UI::SynthSample> DecodeInstrument(const PM64Audio::Instrument& ins) {
    static std::map<const PM64Audio::Instrument*, std::shared_ptr<UI::SynthSample>> sCache;
    const auto it = sCache.find(&ins);
    if (it != sCache.end()) {
        return it->second;
    }
    auto sample = std::make_shared<UI::SynthSample>();
    const size_t npred = ins.book.size() / 16;
    if (npred == 0) {
        sCache[&ins] = sample;
        return sample;
    }
    sample->pcm.reserve(ins.wav.size() / 9 * 16);
    // The expanded codebook propagates the recursion within an 8-sample
    // vector through the residual columns; history stays fixed per vector
    // (sliding it per sample double-applies the filter and diverges).
    int32_t h1 = 0, h2 = 0; // last two samples of the previous vector
    for (size_t f = 0; f + 9 <= ins.wav.size(); f += 9) {
        const uint8_t hdr = ins.wav[f];
        const int32_t scale = 1 << (hdr >> 4);
        const size_t pred = std::min<size_t>(hdr & 0xF, npred - 1);
        const int16_t* c0 = ins.book.data() + pred * 16;
        const int16_t* c1 = c0 + 8;
        int32_t r[16];
        for (int i = 0; i < 8; ++i) {
            const uint8_t b = ins.wav[f + 1 + i];
            int32_t hi = b >> 4, lo = b & 0xF;
            r[i * 2] = (hi > 7 ? hi - 16 : hi) * scale;
            r[i * 2 + 1] = (lo > 7 ? lo - 16 : lo) * scale;
        }
        for (int g = 0; g < 2; ++g) {
            const int32_t* rg = r + g * 8;
            int32_t v[8];
            for (int i = 0; i < 8; ++i) {
                int64_t acc = (int64_t)c0[i] * h2 + (int64_t)c1[i] * h1;
                for (int j = 0; j < i; ++j) {
                    acc += (int64_t)rg[j] * c1[i - j - 1];
                }
                // floor division by 2^11, then the raw residual
                int64_t dout = acc >> 11;
                v[i] = (int32_t)dout + rg[i];
            }
            for (int i = 0; i < 8; ++i) {
                sample->pcm.push_back((int16_t)std::clamp(v[i], -32768, 32767));
            }
            h2 = v[6];
            h1 = v[7];
        }
    }
    if (ins.loopEnd > ins.loopStart && ins.loopCount != 0 && (size_t)ins.loopEnd <= sample->pcm.size()) {
        sample->looped = true;
        sample->loopStart = (uint32_t)ins.loopStart;
        sample->loopEnd = (uint32_t)ins.loopEnd;
    }
    sCache[&ins] = sample;
    return sample;
}

// Envelope command list -> synth breakpoints (levels 0-127 normalized).
// Returns the loop start time (or -1) via loopT.
std::vector<std::pair<float, float>> EnvToPoints(const std::vector<uint8_t>& cmds, float& loopT) {
    std::vector<std::pair<float, float>> points;
    loopT = -1.0f;
    double t = 0.0;
    float scale = 1.0f;
    for (size_t i = 0; i + 1 < cmds.size(); i += 2) {
        const uint8_t a = cmds[i];
        const uint8_t b = cmds[i + 1];
        if (a < 0x80) {
            t += EnvIntervalSec(a);
            points.emplace_back((float)t, (float)(b & 0x7F) / 127.0f * scale);
        } else if (a == 0xFE) { // ENV_CMD_SET_SCALE
            scale = std::min<int>(b, 128) / 128.0f;
        } else if (a == 0xFD) { // ENV_CMD_ADD_SCALE
            scale = std::clamp(scale + (float)(int8_t)b / 128.0f, 0.0f, 1.0f);
        } else if (a == 0xFC) { // ENV_CMD_START_LOOP
            loopT = (float)t;
        } else if (a == 0xFB || a == 0xFF) { // ENV_CMD_END_LOOP / ENV_CMD_END
            break;
        }
    }
    return points;
}

// Release list: total ramp time to silence.
float EnvReleaseSec(const std::vector<uint8_t>& cmds) {
    double t = 0.0;
    for (size_t i = 0; i + 1 < cmds.size(); i += 2) {
        const uint8_t a = cmds[i];
        if (a < 0x80) {
            t += EnvIntervalSec(a);
        } else if (a == 0xFB || a == 0xFF) {
            break;
        }
    }
    return std::max((float)t, 0.005f);
}

const std::vector<uint8_t> kDefaultPress = { 61, 127, 0xFF, 0 };  // 280ms to full
const std::vector<uint8_t> kDefaultRelease = { 52, 0, 0xFF, 0 };  // 550ms to silence

std::string sPlayingName;
UI::RenderedAudio sSamplePcm;
std::string sSampleName;

bool DecodeSampleCard(const ParseResultData& item, int& rate) {
    const auto data = std::static_pointer_cast<PM64BkSampleData>(item.data.value());
    const auto& ins = data->bank->instruments[data->slot];
    rate = ins->sampleRate > 0 ? ins->sampleRate : 32000;
    if (sSampleName == item.name) {
        return !sSamplePcm.pcm.empty();
    }
    sSampleName = item.name;
    sSamplePcm = {};
    const auto sample = DecodeInstrument(*ins);
    sSamplePcm.pcm = sample->pcm;
    return !sSamplePcm.pcm.empty();
}

} // namespace

float PM64BkSampleFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetFrameHeightWithSpacing() * 2.0f +
           ImGui::GetStyle().ItemSpacing.y * 4.0f;
}

void PM64BkSampleFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto data = std::static_pointer_cast<PM64BkSampleData>(item.data.value());
    const auto& ins = data->bank->instruments[data->slot];
    ImGui::TextDisabled("bank %s slot %d  \xe2\x80\x94  %zu bytes vadpcm, %d Hz, key %.1f, loop %d-%d (x%d), %zu envs",
                        data->bank->name, data->slot, ins->wav.size(), ins->sampleRate, ins->keyBase / 100.0f,
                        ins->loopStart, ins->loopEnd, ins->loopCount, ins->envelopes.size());

    int rate = 32000;
    const bool playingThis = sPlayingName == item.name && UI::GetBackend()->AudioProgress() >= 0.0f;
    if (ImGui::Button(playingThis ? "Stop##bksample" : "Play##bksample")) {
        if (playingThis) {
            UI::GetBackend()->StopAudio();
            sPlayingName.clear();
        } else if (DecodeSampleCard(item, rate)) {
            UI::GetBackend()->SetAudioSpeed(1.0f);
            if (UI::GetBackend()->PlaySamples(sSamplePcm.pcm.data(), sSamplePcm.pcm.size(), rate, 1)) {
                sPlayingName = item.name;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("WAV##bksampleexp")) {
        if (DecodeSampleCard(item, rate)) {
            const auto path = UI::ExportFilePath(item.name, "wav");
            UI::NoteExport(item.name,
                           UI::WriteWavFile(path, sSamplePcm.pcm.data(), sSamplePcm.pcm.size(), 1, rate)
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
    if (ImGui::SliderFloat("##bkvol", &volume, 0.0f, 1.0f, "vol %.2f")) {
        UI::GetBackend()->SetAudioVolume(volume);
    }
}

// --- BGM sequence driver ------------------------------------------------------

namespace {

// pmret BgmTicksRates, indexed by BGMFileInfo.timingPreset & 7.
const int kTickRates[8] = { 48, 24, 32, 40, 48, 56, 64, 48 };
// pmret SeqCmdArgCounts for opcodes E0-FF.
const int kArgCounts[32] = { 2, 1, 1, 1, 4, 3, 2, 0, 2, 1, 1, 1, 1, 1, 1, 2,
                             3, 1, 1, 0, 2, 1, 3, 1, 0, 0, 0, 0, 3, 3, 3, 3 };
// pmret polyphonyCounts by track polyphonicIdx.
const int kPolyphony[8] = { 0, 1, 0, 0, 0, 2, 3, 4 };

struct BgmBankMap {
    // (bankSetIndex << 8 | bankSlot) -> bank
    std::map<uint32_t, std::shared_ptr<PM64Audio::Bank>> banks;

    const PM64Audio::Instrument* Get(uint32_t bankByte, uint32_t patch, int& envIdx) const {
        envIdx = bankByte & 3;
        const uint32_t setIdx = (bankByte & 0x70) >> 4;
        const auto it = banks.find((setIdx << 8) | (patch >> 4));
        if (it == banks.end() || it->second == nullptr) {
            return nullptr;
        }
        return it->second->instruments[patch & 15].get();
    }
};

struct BgmTrack {
    uint32_t pos = 0;
    bool active = false;
    bool muted = false;
    bool isDrum = false;
    int delay = 0;
    uint32_t savedPos = 0; // detour return
    int detourLen = 0;
    uint32_t prevPos = 0;  // branch return
    int voiceCount = 1;

    const PM64Audio::Instrument* ins = nullptr;
    const std::vector<uint8_t>* envPress = nullptr;
    const std::vector<uint8_t>* envRelease = nullptr;
    double insVolume = 1.0;
    double insVolTarget = 1.0, insVolStep = 0.0;
    int insVolTicks = 0;
    int volume = 127;
    int pan = 64;
    int reverb = 0;
    int coarse = 0, fine = 0, detune = 0;
    int tremDelay = 0, tremRate = 0, tremDepth = 0;
    bool autoDirty = true;

    struct Voice {
        int note = -1; // index into the notes vector
        int remain = 0;
    };
    std::vector<Voice> voices;
};

double CentsRatio(double cents) {
    return std::pow(2.0, cents / 1200.0);
}

} // namespace

std::vector<std::string> SequencePlayerPM64::Options(const ParseResultData& item) {
    std::vector<std::string> options;
    if (!item.data.has_value()) {
        return options;
    }
    const auto& file = std::static_pointer_cast<PM64BgmData>(item.data.value())->file;
    const auto rd16 = [&file](uint64_t off) -> uint16_t {
        return off + 2 <= file.size() ? (uint16_t)((file[off] << 8) | file[off + 1]) : 0;
    };
    for (int i = 0; i < 4; ++i) {
        if (rd16(0x14 + i * 2) != 0) {
            options.push_back("variation " + std::to_string(i));
        }
    }
    if (options.size() < 2) {
        options.clear(); // hide the picker for single-variation songs
    }
    return options;
}

bool SequencePlayerPM64::Render(const ParseResultData& item, int option, UI::RenderedAudio& out) {
    if (!item.data.has_value()) {
        return false;
    }
    const auto& file = std::static_pointer_cast<PM64BgmData>(item.data.value())->file;
    const auto rd8 = [&file](uint64_t off) -> uint8_t { return off < file.size() ? file[off] : 0; };
    const auto rd16 = [&file](uint64_t off) -> uint16_t {
        return off + 2 <= file.size() ? (uint16_t)((file[off] << 8) | file[off + 1]) : 0;
    };
    const auto rd32 = [&file](uint64_t off) -> uint32_t {
        return off + 4 <= file.size() ? ((uint32_t)file[off] << 24) | ((uint32_t)file[off + 1] << 16) |
                                            ((uint32_t)file[off + 2] << 8) | file[off + 3]
                                      : 0;
    };

    // Bank bindings from the registered node.
    BgmBankMap bankMap;
    const YAML::Node banksNode = item.node["banks"];
    if (banksNode.IsSequence()) {
        for (size_t i = 0; i + 1 < banksNode.size(); i += 2) {
            const uint32_t key = banksNode[i].as<uint32_t>();
            const uint32_t romOff = banksNode[i + 1].as<uint32_t>();
            const auto bit = PM64Audio::BankRegistry().find(romOff);
            if (bit != PM64Audio::BankRegistry().end()) {
                bankMap.banks[key] = bit->second;
            }
        }
    }

    // Header (BGMFileInfo at 0x10).
    const int timingPreset = rd8(0x10);
    uint16_t compositions[4];
    int variations = 0;
    uint16_t chosen = 0;
    for (int i = 0; i < 4; ++i) {
        compositions[i] = rd16(0x14 + i * 2);
        if (compositions[i] != 0 && variations++ == option) {
            chosen = compositions[i];
        }
    }
    if (chosen == 0) {
        chosen = compositions[0];
    }
    if (chosen == 0) {
        return false;
    }
    const uint32_t drumsOff = (uint32_t)rd16(0x1C) << 2;
    const int drumCount = rd16(0x1E);
    const uint32_t insOff = (uint32_t)rd16(0x20) << 2;
    const int insCount = rd16(0x22);

    const int ticksPerBeat = kTickRates[timingPreset & 7];
    // pmret au_bgm_set_tick_resolution: milli-frames per tick, from the 5.75ms
    // audio frame cadence, bounds the usable tempo range.
    const double mframesPerMinute = 1000.0 * (60.0 * 32000.0 / 184.0);
    double mframesPerTick = mframesPerMinute / ticksPerBeat;
    mframesPerTick = std::clamp(mframesPerTick, 80000.0, 500000.0);
    const double maxTempo = (mframesPerTick / 1000.0) * 100.0; // BPM*100 cap

    // Master state.
    double masterTempo = std::min(156.0 * 100.0, maxTempo);
    double tempoTarget = 0.0, tempoStep = 0.0;
    int tempoTicks = 0;
    double masterVolume = 1.0;
    double volTarget = 0.0, volStep = 0.0;
    int volTicks = 0;
    int masterPitchShift = 0;

    // Composition stream (u32 words, offsets <<2 relative to the file).
    const uint32_t compBase = (uint32_t)chosen << 2;
    uint32_t compPos = compBase;
    uint32_t labels[32];
    for (auto& l : labels) {
        l = compBase;
    }
    for (uint32_t scan = compBase;;) {
        const uint32_t cmd = rd32(scan);
        scan += 4;
        if (cmd == 0 || scan >= file.size()) {
            break;
        }
        if ((cmd >> 28) == 3) { // BGM_COMP_START_LOOP
            labels[cmd & 0x1F] = scan;
        }
    }
    uint32_t loopEndPos[5] = {};
    int loopCounters[5] = {};
    int loopDepth = 0;
    // Count-0 composition loops repeat forever in-game; the preview passes
    // through and plays the body once.

    std::vector<BgmTrack> tracks(16);
    int skippedNoIns = 0, skippedDrum = 0;
    std::map<uint32_t, int> missingBank;
    std::vector<UI::SynthNote> notes;
    UI::GainAutomation gainAuto[16];
    UI::PitchAutomation pitchAuto[16];

    double now = 0.0;
    double tickSec = 60.0 / ((masterTempo / 100.0) * ticksPerBeat);
    enum { FETCH, ACTIVE, DONE } state = FETCH;
    const double kMaxSeconds = 300.0;
    long safety = 4000000;

    const auto pushGain = [&](int t) {
        gainAuto[t].emplace_back((float)now, (float)(masterVolume * tracks[t].insVolume));
    };

    const auto killVoice = [&](BgmTrack& tr, int v) {
        if (tr.voices[v].note >= 0) {
            UI::SynthNote& n = notes[tr.voices[v].note];
            n.soundSec = std::max(0.001, now - n.startSec);
            n.releaseSec = 0.01;
            tr.voices[v].note = -1;
        }
    };

    while (state != DONE && now < kMaxSeconds && safety-- > 0) {
        if (state == FETCH) {
            const uint32_t cmd = rd32(compPos);
            compPos += 4;
            if (cmd == 0 || compPos > file.size()) {
                state = DONE;
                break;
            }
            switch (cmd >> 28) {
                case 1: { // BGM_COMP_PLAY_PHRASE
                    const uint32_t phraseStart = compBase + ((cmd & 0xFFFF) << 2);
                    for (int i = 0; i < 16; ++i) {
                        BgmTrack& tr = tracks[i];
                        tr.voices.clear();
                        const uint32_t info = rd32(phraseStart + i * 4);
                        const uint32_t rel = info >> 16;
                        tr.active = false;
                        if (rel != 0 && (info & 0x100) == 0) {
                            const uint32_t linked = (info >> 9) & 0xF;
                            if (linked != 0) {
                                // Linked tracks start muted (proximity mixes).
                                continue;
                            }
                            tr.active = true;
                            tr.pos = phraseStart + rel;
                            tr.delay = 1;
                            tr.isDrum = (info >> 7) & 1;
                            tr.voiceCount = kPolyphony[(info >> 13) & 7];
                            tr.voices.assign(tr.voiceCount, BgmTrack::Voice{});
                            tr.savedPos = 0;
                            tr.detourLen = 0;
                            tr.prevPos = 0;
                        }
                    }
                    state = ACTIVE;
                    break;
                }
                case 3: // BGM_COMP_START_LOOP
                    break;
                case 4: // BGM_COMP_WAIT: consume one tick
                    now += tickSec;
                    break;
                case 5:   // BGM_COMP_END_LOOP
                case 6:   // conditional variants (flag defaults false -> case 6 loops)
                case 7: {
                    if ((cmd >> 28) == 7) {
                        break; // loop-if-true with flag false: skip
                    }
                    const int label = cmd & 0x1F;
                    const int iter = (cmd >> 5) & 0x7F;
                    if (iter == 0) {
                        break; // infinite loop: play through once for preview
                    }
                    int depth = loopDepth;
                    if (loopEndPos[depth] != 0 && loopEndPos[depth] == compPos) {
                        if (loopCounters[depth] != 0) {
                            loopCounters[depth]--;
                            if (loopCounters[depth] == 0) {
                                loopEndPos[depth] = 0;
                                if (depth > 0) {
                                    depth--;
                                }
                            } else {
                                compPos = labels[label];
                            }
                        } else {
                            compPos = labels[label];
                        }
                    } else if (loopEndPos[depth] == 0) {
                        loopEndPos[depth] = compPos;
                        loopCounters[depth] = iter;
                        compPos = labels[label];
                    } else if (depth < 4) {
                        depth++;
                        loopEndPos[depth] = compPos;
                        loopCounters[depth] = iter;
                        compPos = labels[label];
                    }
                    loopDepth = depth;
                    break;
                }
                default:
                    state = DONE;
                    break;
            }
            continue;
        }

        // --- one tick (au_bgm_player_update_playing) ---
        bool finished = false;

        if (tempoTicks != 0) {
            tempoTicks--;
            masterTempo = tempoTicks == 0 ? tempoTarget : masterTempo + tempoStep;
            masterTempo = std::clamp(masterTempo, 100.0, maxTempo);
            tickSec = 60.0 / ((masterTempo / 100.0) * ticksPerBeat);
        }
        bool masterVolChanged = false;
        if (volTicks != 0) {
            volTicks--;
            masterVolume = volTicks == 0 ? volTarget : masterVolume + volStep;
            masterVolChanged = true;
        }

        for (int ti = 0; ti < 16; ++ti) {
            BgmTrack& tr = tracks[ti];
            if (!tr.active) {
                continue;
            }
            if (tr.insVolTicks != 0) {
                tr.insVolTicks--;
                tr.insVolume = tr.insVolTicks == 0 ? tr.insVolTarget : tr.insVolume + tr.insVolStep;
                tr.autoDirty = true;
            }
            if (masterVolChanged || tr.autoDirty) {
                pushGain(ti);
                tr.autoDirty = false;
            }

            // Byte reader with detour bookkeeping (POST_BGM_READ).
            const auto readByte = [&]() -> uint8_t {
                const uint8_t b = rd8(tr.pos);
                tr.pos++;
                if (tr.detourLen != 0) {
                    tr.detourLen--;
                    if (tr.detourLen == 0) {
                        tr.pos = tr.savedPos;
                    }
                }
                return b;
            };

            tr.delay--;
            int guard = 4096;
            while (tr.delay == 0 && guard-- > 0) {
                const uint8_t op = readByte();
                if (op < 0x80) {
                    if (op == 0) {
                        if (tr.prevPos != 0) {
                            tr.pos = tr.prevPos;
                            tr.prevPos = 0;
                        } else {
                            finished = true;
                            break;
                        }
                    } else if (op >= 0x78) {
                        tr.delay = ((op & 7) << 8) + readByte() + 0x78;
                    } else {
                        tr.delay = op;
                    }
                } else if (op < 0xD4) {
                    // note
                    const int pitch = op & 0x7F;
                    const int velocity = readByte();
                    int length = readByte();
                    if (length >= 0xC0) {
                        length = ((length & ~0xC0) << 8) + readByte() + 0xC0;
                    }
                    if (tr.muted || length <= 1 || tr.voices.empty()) {
                        continue;
                    }
                    // acquire a voice: first free, else steal shortest
                    int slot = -1;
                    for (size_t v = 0; v < tr.voices.size(); ++v) {
                        if (tr.voices[v].note < 0) {
                            slot = (int)v;
                            break;
                        }
                    }
                    if (slot < 0) {
                        int shortest = INT32_MAX;
                        for (size_t v = 0; v < tr.voices.size(); ++v) {
                            if (tr.voices[v].remain < shortest) {
                                shortest = tr.voices[v].remain;
                                slot = (int)v;
                            }
                        }
                        killVoice(tr, slot);
                    }

                    const PM64Audio::Instrument* ins = tr.ins;
                    const std::vector<uint8_t>* press = tr.envPress;
                    const std::vector<uint8_t>* release = tr.envRelease;
                    double cents;
                    double gain = (tr.volume / 127.0) * ((velocity > 0 ? velocity + 1 : 0) / 128.0);
                    double pan = tr.pan;
                    double reverb = tr.reverb;
                    if (tr.isDrum) {
                        const PM64Audio::DrumInfo* drum = nullptr;
                        if (pitch < 72) {
                            if ((size_t)pitch < PM64Audio::Globals().perDrums.size()) {
                                drum = &PM64Audio::Globals().perDrums[pitch];
                            }
                        } else if (pitch - 72 < drumCount) {
                            static PM64Audio::DrumInfo sBgmDrum;
                            const uint64_t d = drumsOff + (uint64_t)(pitch - 72) * 0x0C;
                            sBgmDrum.bankPatch = rd16(d);
                            sBgmDrum.keyBase = rd16(d + 2);
                            sBgmDrum.volume = rd8(d + 4);
                            sBgmDrum.pan = (int8_t)rd8(d + 5);
                            sBgmDrum.reverb = rd8(d + 6);
                            drum = &sBgmDrum;
                        }
                        if (drum == nullptr) {
                            skippedDrum++;
                            continue;
                        }
                        int envIdx = 0;
                        ins = bankMap.Get(drum->bankPatch >> 8, drum->bankPatch & 0xFF, envIdx);
                        if (ins == nullptr) {
                            missingBank[drum->bankPatch]++;
                            continue;
                        }
                        if (envIdx < (int)ins->envelopes.size()) {
                            press = &ins->envelopes[envIdx].first;
                            release = &ins->envelopes[envIdx].second;
                        } else {
                            press = release = nullptr;
                        }
                        gain *= drum->volume / 127.0;
                        pan = drum->pan;
                        reverb = drum->reverb;
                        cents = (double)drum->keyBase + tr.coarse + tr.fine - ins->keyBase;
                    } else {
                        if (ins == nullptr) {
                            skippedNoIns++;
                            continue;
                        }
                        cents = pitch * 100.0 + tr.coarse + masterPitchShift + tr.fine - ins->keyBase;
                    }
                    cents += tr.detune;

                    UI::SynthNote note;
                    note.startSec = now;
                    note.soundSec = length * tickSec; // re-measured at expiry (tempo fades)
                    note.chan = ti;
                    note.gain = (float)gain;
                    note.pan = (float)std::clamp(pan / 127.0, 0.0, 1.0);
                    note.reverb = (float)(reverb / 127.0) * 0.5f;
                    note.sample = DecodeInstrument(*ins);
                    const int rate = ins->sampleRate > 0 ? ins->sampleRate : 32000;
                    note.freqScale = (float)(CentsRatio(cents) * rate / UI::kSynthRate);
                    note.envPoints = EnvToPoints(press != nullptr ? *press : kDefaultPress, note.envLoopStartT);
                    note.releaseSec = EnvReleaseSec(release != nullptr ? *release : kDefaultRelease);
                    if (tr.tremDepth != 0) {
                        note.vibDelaySec = (float)(tr.tremDelay * tickSec);
                        const float hz = (float)((tr.tremRate / 256.0) / tickSec);
                        note.vibRateStartHz = note.vibRateEndHz = hz;
                        // triangle amplitude is ~(1024 * depth) >> 8 cents
                        note.vibDepthStart = note.vibDepthEnd = (float)(tr.tremDepth * 4.0 / 100.0);
                    }
                    if (std::getenv("TORCH_BGM_DEBUG") != nullptr && notes.size() < 120) {
                        fprintf(stderr,
                                "[bgm] t=%7.3f trk=%2d pitch=%3d vel=%3d len=%4d cents=%7.0f step=%.4f gain=%.3f "
                                "rate=%d key=%d env=%zupt rel=%.2f drum=%d\n",
                                now, ti, pitch, velocity, length, cents, note.freqScale, note.gain,
                                ins->sampleRate, ins->keyBase, note.envPoints.size(), note.releaseSec, (int)tr.isDrum);
                    }
                    tr.voices[slot].note = (int)notes.size();
                    tr.voices[slot].remain = length;
                    notes.push_back(std::move(note));
                } else {
                    // command E0-FF (D4-DF unused in retail data)
                    const int idx = op >= 0xE0 ? op - 0xE0 : 0;
                    uint8_t args[4] = {};
                    const int count = op >= 0xE0 ? kArgCounts[idx] : 0;
                    for (int a = 0; a < count; ++a) {
                        args[a] = readByte();
                    }
                    switch (op) {
                        case 0xE0: { // MasterTempo
                            masterTempo = std::clamp((double)((args[0] << 8) | args[1]) * 100.0, 100.0, maxTempo);
                            tickSec = 60.0 / ((masterTempo / 100.0) * ticksPerBeat);
                            tempoTicks = 0;
                            break;
                        }
                        case 0xE1: // MasterVolume
                            masterVolume = (args[0] & 0x7F) / 127.0;
                            volTicks = 0;
                            pushGain(ti);
                            break;
                        case 0xE2: // MasterDetune (cents, signed semis*100)
                            masterPitchShift = (int8_t)args[0] * 100;
                            break;
                        case 0xE4: { // MasterTempoFade
                            int time = (args[0] << 8) | args[1];
                            const double target =
                                std::clamp((double)((args[2] << 8) | args[3]) * 100.0, 100.0, maxTempo);
                            if (time <= 0) {
                                time = 1;
                            }
                            tempoTicks = time;
                            tempoTarget = target;
                            tempoStep = (target - masterTempo) / time;
                            break;
                        }
                        case 0xE5: { // MasterVolumeFade
                            int time = (args[0] << 8) | args[1];
                            const double target = (args[2] & 0x7F) / 127.0;
                            if (time <= 0) {
                                time = 1;
                            }
                            volTicks = time;
                            volTarget = target;
                            volStep = (target - masterVolume) / time;
                            break;
                        }
                        case 0xE8: { // TrackOverridePatch
                            int envIdx = 0;
                            tr.ins = bankMap.Get(args[0], args[1], envIdx);
                            tr.envPress = tr.envRelease = nullptr;
                            if (tr.ins != nullptr && envIdx < (int)tr.ins->envelopes.size()) {
                                tr.envPress = &tr.ins->envelopes[envIdx].first;
                                tr.envRelease = &tr.ins->envelopes[envIdx].second;
                            }
                            break;
                        }
                        case 0xE9: // InstrumentVolume
                            tr.insVolume = (args[0] & 0x7F) / 127.0;
                            tr.insVolTicks = 0;
                            pushGain(ti);
                            break;
                        case 0xEA: // InstrumentPan
                            tr.pan = args[0] & 0x7F;
                            break;
                        case 0xEB: // InstrumentReverb
                            tr.reverb = args[0] & 0x7F;
                            break;
                        case 0xEC: // TrackVolume
                            tr.volume = args[0] & 0x7F;
                            break;
                        case 0xED: // InstrumentCoarseTune (semitones)
                            tr.coarse = (int8_t)args[0] * 100;
                            break;
                        case 0xEE: // InstrumentFineTune (cents)
                            tr.fine = (int8_t)args[0];
                            break;
                        case 0xEF: // TrackDetune (s16 cents)
                            tr.detune = (int16_t)((args[0] << 8) | args[1]);
                            break;
                        case 0xF0: // TrackTremolo
                            tr.tremDelay = args[0];
                            tr.tremRate = args[1];
                            tr.tremDepth = args[2];
                            break;
                        case 0xF1:
                            tr.tremRate = args[0];
                            break;
                        case 0xF2:
                            tr.tremDepth = args[0];
                            break;
                        case 0xF3:
                            tr.tremDepth = 0;
                            break;
                        case 0xF4: // SubTrackRandomPan (use base pan)
                            tr.pan = args[0] & 0x7F;
                            break;
                        case 0xF5: { // UseInstrument
                            PM64Audio::ProgramInfo info;
                            const uint32_t index = args[0];
                            bool have = false;
                            if (index < 0x80) {
                                if ((int)index < insCount) {
                                    const uint64_t p = insOff + (uint64_t)index * 8;
                                    info.bankPatch = rd16(p);
                                    info.volume = rd8(p + 2);
                                    info.pan = (int8_t)rd8(p + 3);
                                    info.reverb = rd8(p + 4);
                                    info.coarseTune = (int8_t)rd8(p + 5);
                                    info.fineTune = (int8_t)rd8(p + 6);
                                    have = true;
                                }
                            } else if (index - 0x80 < PM64Audio::Globals().prgPrograms.size()) {
                                info = PM64Audio::Globals().prgPrograms[index - 0x80];
                                have = true;
                            }
                            if (!have) {
                                break;
                            }
                            int envIdx = 0;
                            tr.ins = bankMap.Get(info.bankPatch >> 8, info.bankPatch & 0xFF, envIdx);
                            tr.envPress = tr.envRelease = nullptr;
                            if (tr.ins != nullptr && envIdx < (int)tr.ins->envelopes.size()) {
                                tr.envPress = &tr.ins->envelopes[envIdx].first;
                                tr.envRelease = &tr.ins->envelopes[envIdx].second;
                            }
                            tr.insVolume = (info.volume & 0x7F) / 127.0;
                            tr.insVolTicks = 0;
                            tr.pan = info.pan & 0x7F;
                            tr.reverb = info.reverb & 0x7F;
                            tr.coarse = info.coarseTune * 100;
                            tr.fine = info.fineTune;
                            pushGain(ti);
                            break;
                        }
                        case 0xF6: { // InstrumentVolumeLerp
                            int time = (args[0] << 8) | args[1];
                            const double target = (args[2] & 0x7F) / 127.0;
                            if (target != tr.insVolume) {
                                if (time <= 0) {
                                    time = 1;
                                }
                                tr.insVolTicks = time;
                                tr.insVolTarget = target;
                                tr.insVolStep = (target - tr.insVolume) / time;
                            }
                            break;
                        }
                        case 0xFC: { // Branch (jump table; prox mix id 0)
                            const uint32_t table = (args[0] << 8) | args[1];
                            tr.prevPos = tr.pos;
                            tr.pos = ((uint32_t)rd8(table) << 8) | rd8(table + 1);
                            tr.isDrum = rd8(table + 2) != 0;
                            tr.coarse = 0;
                            tr.fine = 0;
                            tr.detune = 0;
                            tr.tremDepth = 0;
                            tr.insVolTicks = 0;
                            break;
                        }
                        case 0xFE: { // Detour
                            const uint32_t off = (args[0] << 8) | args[1];
                            tr.detourLen = args[2];
                            tr.savedPos = tr.pos;
                            tr.pos = off;
                            break;
                        }
                        default: // E3/E6/E7/F7-FB/FD/FF: effects, events; no-op here
                            break;
                    }
                }
            }
            if (finished) {
                break;
            }

            // decrement sounding notes; finalize on expiry
            for (auto& v : tr.voices) {
                if (v.note >= 0) {
                    if (--v.remain <= 0) {
                        notes[v.note].soundSec = std::max(0.001, now + tickSec - notes[v.note].startSec);
                        v.note = -1;
                    }
                }
            }
        }

        now += tickSec;
        if (finished) {
            state = FETCH;
        }
    }

    if (std::getenv("TORCH_BGM_DEBUG") != nullptr) {
        std::string missing;
        for (const auto& [bp, count] : missingBank) {
            char buf[32];
            snprintf(buf, sizeof(buf), " %04X:%d", bp, count);
            missing += buf;
        }
        fprintf(stderr, "[bgmstat] %s notes=%zu noIns=%d noDrum=%d missingBank:%s\n", item.name.c_str(),
                notes.size(), skippedNoIns, skippedDrum, missing.c_str());
    }
    if (notes.empty()) {
        return false;
    }
    out.pcm = UI::Synthesize(notes, gainAuto, pitchAuto, now);
    out.noteCount = notes.size();
    return !out.pcm.empty();
}

#endif // BUILD_UI
