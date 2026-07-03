#ifdef BUILD_UI

#include "SequencePlayerV1.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <unordered_map>

#include "AudioContext.h"
#include "AudioConverter.h"
#include "BookFactory.h"
#include "DrumFactory.h"
#include "EnvelopeFactory.h"
#include "InstrumentFactory.h"
#include "LoopFactory.h"
#include "SampleFactory.h"
#include "SoundFontFactory.h"
#include "factories/naudio/v0/AIFCDecode.h"
#include "factories/naudio/v0/SampleFactory.h"
#include "factories/GenericArrayFactory.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "binarytools/endianness.h"

#ifdef SF64_SUPPORT
#include "factories/sf64/audio/AudioDecompressor.h"
#endif

namespace {

constexpr int kMaxTicks = 60000;
constexpr size_t kMaxEvents = 60000;

using NoteEvent = UI::SynthNote;

// Parsed-asset graph, resolved once: fonts by id, referenced assets by offset.
struct FontRef {
    const SoundFontData* font = nullptr;
    std::string name;
    uint32_t id = 0;
    std::vector<const InstrumentData*> insts;
    std::vector<const DrumData*> drums;
};

struct AssetIndex {
    std::vector<FontRef> fonts;
    std::unordered_map<uint32_t, const InstrumentData*> instByOffset;
    std::unordered_map<uint32_t, const DrumData*> drumByOffset;
    std::unordered_map<uint32_t, NSampleData*> sampleByOffset;
    std::unordered_map<std::string, NSampleData*> sampleByName;
    std::unordered_map<uint32_t, const EnvelopeData*> envByOffset;
    std::unordered_map<uint32_t, const ADPCMLoopData*> loopByOffset;
    std::unordered_map<uint32_t, const ADPCMBookData*> bookByOffset;
    // Per-sample reference tuning: of every tuning that references the sample
    // (instrument slots + drums), the one closest to 1.0. Natural rate is
    // tuning * 32000.
    std::unordered_map<const NSampleData*, float> refTuning;
    // gSeqFontTable blob: u16 offsets per seqId, then {count, fontIds...}.
    std::vector<uint8_t> seqFontTable;
};

uint32_t NodeOffset(const ParseResultData& r) {
    return GetSafeNode<uint32_t>(const_cast<ParseResultData&>(r).node, "offset", 0);
}

AssetIndex& Assets() {
    static AssetIndex index;
    static bool built = false;
    if (built) {
        return index;
    }
    built = true;

    std::vector<std::pair<uint32_t, FontRef>> fonts;
    for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
        for (const auto& r : results) {
            if (!r.data.has_value()) {
                continue;
            }
            const uint32_t offset = NodeOffset(r);
            if (r.type == "NAUDIO:V1:INSTRUMENT") {
                index.instByOffset[offset] = std::static_pointer_cast<InstrumentData>(r.data.value()).get();
            } else if (r.type == "NAUDIO:V1:DRUM") {
                index.drumByOffset[offset] = std::static_pointer_cast<DrumData>(r.data.value()).get();
            } else if (r.type == "NAUDIO:V1:SAMPLE") {
                auto* sample = std::static_pointer_cast<NSampleData>(r.data.value()).get();
                index.sampleByOffset[offset] = sample;
                index.sampleByName[r.name] = sample;
            } else if (r.type == "NAUDIO:V1:ENVELOPE") {
                index.envByOffset[offset] = std::static_pointer_cast<EnvelopeData>(r.data.value()).get();
            } else if (r.type == "NAUDIO:V1:ADPCM_LOOP") {
                index.loopByOffset[offset] = std::static_pointer_cast<ADPCMLoopData>(r.data.value()).get();
            } else if (r.type == "NAUDIO:V1:ADPCM_BOOK") {
                index.bookByOffset[offset] = std::static_pointer_cast<ADPCMBookData>(r.data.value()).get();
            } else if (r.type == "ARRAY") {
                std::string base = r.name;
                std::transform(base.begin(), base.end(), base.begin(), ::tolower);
                base.erase(std::remove(base.begin(), base.end(), '_'), base.end());
                if (base.find("seqfont") != std::string::npos) {
                    for (const auto& datum : std::static_pointer_cast<GenericArray>(r.data.value())->mData) {
                        if (std::holds_alternative<uint8_t>(datum)) {
                            index.seqFontTable.push_back(std::get<uint8_t>(datum));
                        }
                    }
                }
            } else if (r.type == "NAUDIO:V1:SOUND_FONT") {
                FontRef ref;
                ref.font = std::static_pointer_cast<SoundFontData>(r.data.value()).get();
                ref.name = r.name;
                fonts.emplace_back(GetSafeNode<uint32_t>(const_cast<ParseResultData&>(r).node, "id", 0), ref);
            }
        }
    }
    // Resolve dedup-suppressed addresses to their canonical sample.
    const auto resolve = [&](uint32_t addr) -> const NSampleData* {
        const auto it = index.sampleByOffset.find(addr);
        if (it != index.sampleByOffset.end()) {
            return it->second;
        }
        const auto rit = AudioContext::sampleAddrRemap.find(addr);
        if (rit != AudioContext::sampleAddrRemap.end()) {
            const auto nit = index.sampleByName.find(rit->second);
            if (nit != index.sampleByName.end()) {
                return nit->second;
            }
        }
        return nullptr;
    };
    const auto addTuning = [&](uint32_t addr, float tuning) {
        const NSampleData* sample = resolve(addr);
        if (sample == nullptr || tuning <= 0.0f) {
            return;
        }
        const auto it = index.refTuning.find(sample);
        if (it == index.refTuning.end() || std::fabs(tuning - 1.0f) < std::fabs(it->second - 1.0f)) {
            index.refTuning[sample] = tuning;
        }
    };
    for (const auto& [off, inst] : index.instByOffset) {
        addTuning(inst->lowPitchTunedSample.sample, inst->lowPitchTunedSample.tuning);
        addTuning(inst->normalPitchTunedSample.sample, inst->normalPitchTunedSample.tuning);
        addTuning(inst->highPitchTunedSample.sample, inst->highPitchTunedSample.tuning);
    }
    for (const auto& [off, drum] : index.drumByOffset) {
        addTuning(drum->tunedSample.sample, drum->tunedSample.tuning);
    }

    std::sort(fonts.begin(), fonts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [id, ref] : fonts) {
        ref.id = id;
        for (const uint32_t addr : ref.font->instruments) {
            auto it = index.instByOffset.find(addr);
            ref.insts.push_back(it != index.instByOffset.end() ? it->second : nullptr);
        }
        for (const uint32_t addr : ref.font->drums) {
            auto it = index.drumByOffset.find(addr);
            ref.drums.push_back(it != index.drumByOffset.end() ? it->second : nullptr);
        }
        index.fonts.push_back(std::move(ref));
    }
    return index;
}

// Dedup-suppressed samples only exist under their canonical path.
NSampleData* SampleByAddr(uint32_t addr) {
    auto& index = Assets();
    const auto it = index.sampleByOffset.find(addr);
    if (it != index.sampleByOffset.end()) {
        return it->second;
    }
    const auto rit = AudioContext::sampleAddrRemap.find(addr);
    if (rit != AudioContext::sampleAddrRemap.end()) {
        const auto nit = index.sampleByName.find(rit->second);
        if (nit != index.sampleByName.end()) {
            return nit->second;
        }
    }
    return nullptr;
}

// fadeOutVel = decayIndex/2560 against 1.0 at 180 ticks/s.
// Engine timing from the audio spec: ticksPerUpdate derives from the
// session frequency (60 fps frames, 192-sample chunks), the ADSR runs at
// 60 * ticksPerUpdate ticks/s.
int TicksPerUpdate() {
    const uint32_t samplesPerFrame = ((AudioContext::sessionFrequency / 60) + 15) & ~15u;
    return (int)((samplesPerFrame + 16) / 192 + 1);
}

float AdsrTickRate() {
    return 60.0f * (float)TicksPerUpdate();
}

float ReleaseSeconds(uint8_t decayIndex) {
    if (decayIndex == 0) {
        return 0.016f; // one-frame fade
    }
    return 2560.0f / ((float)decayIndex * AdsrTickRate());
}

std::shared_ptr<UI::SynthSample> SynthSampleFor(NSampleData* sample) {
    static std::unordered_map<NSampleData*, std::shared_ptr<UI::SynthSample>> sCache;
    if (sample == nullptr) {
        return nullptr;
    }
    auto it = sCache.find(sample);
    if (it != sCache.end()) {
        return it->second;
    }
    auto synth = std::make_shared<UI::SynthSample>();

#ifdef SF64_SUPPORT
    if (AudioContext::driver == NAudioDrivers::SF64 && sample->codec == 2) {
        auto& table = AudioContext::tables[AudioTableType::SAMPLE_TABLE];
        const uint8_t* ptr = table.buffer.data() + table.info->entries[sample->sampleBankId].addr + sample->sampleAddr;
        std::vector<uint8_t> raw(ptr, ptr + sample->size);
        std::vector<int16_t> out(sample->size * 2, 0);
        SF64::DecompressAudio(raw, out.data());
        synth->pcm.assign(out.begin(), out.begin() + sample->size);
    } else
#endif
    {
        auto& assets = Assets();
        const auto lit0 = assets.loopByOffset.find(sample->loop);
        const auto bit = assets.bookByOffset.find(sample->book);
        if (lit0 == assets.loopByOffset.end() || bit == assets.bookByOffset.end()) {
            SPDLOG_WARN("Sample missing loop/book asset (loop 0x{:X}, book 0x{:X})", sample->loop, sample->book);
            return sCache.emplace(sample, std::move(synth)).first->second;
        }
        LUS::BinaryWriter aifc;
        AudioConverter::SampleV1ToAIFC(sample, lit0->second, bit->second, aifc);
        const auto bytes = aifc.ToVector();
        if (!bytes.empty()) {
            LUS::BinaryWriter aiff;
            write_aiff(bytes, aiff);
            int rate = 0;
            const bool ok = DecodeAiffBytes(aiff.ToVector(), synth->pcm, rate);
            if ((!ok || synth->pcm.empty()) && std::getenv("TORCH_SEQ_DEBUG") != nullptr) {
                printf("[sample] DECODE FAIL addr=0x%X book(order=%d npred=%d coeffs=%zu) loop(%u..%u x%u) aifc=%zu\n",
                       sample->sampleAddr, bit->second->order, bit->second->numPredictors, bit->second->book.size(),
                       lit0->second->start, lit0->second->end, lit0->second->count, bytes.size());
            }
            aiff.Close();
            const size_t expected = (size_t)sample->size * 16 / 9;
            if (synth->pcm.size() < expected) {
                synth->pcm.insert(synth->pcm.begin(), expected - synth->pcm.size(), 0);
            }
        }
        aifc.Close();
    }

    const auto lit = Assets().loopByOffset.find(sample->loop);
    if (lit != Assets().loopByOffset.end()) {
        synth->loopStart = lit->second->start;
        synth->loopEnd = lit->second->end;
        synth->looped = lit->second->count != 0;
    }
    if (std::getenv("TORCH_SEQ_DEBUG") != nullptr) {
        static int sDumped = 0;
        float pk = 0;
        for (size_t i = 0; i < synth->pcm.size(); ++i)
            pk = std::max(pk, std::fabs((float)synth->pcm[i]));
        if (sDumped++ < 8) {
            printf("[sample] codec=%u size=%u addr=0x%X bank=%u loop=0x%X book=0x%X tuning=%.3f pcm=%zu peak=%.0f\n",
                   (uint32_t)sample->codec, (uint32_t)sample->size, sample->sampleAddr, sample->sampleBankId,
                   sample->loop, sample->book, sample->tuning, synth->pcm.size(), pk);
        }
    }
    return sCache.emplace(sample, std::move(synth)).first->second;
}

// Envelope points: delay in 1/240s updates, level squared; -2/-3 loop.
void FillEnvelopePoints(UI::SynthNote& ev, const std::vector<EnvelopePoint>& points) {
    std::vector<int> entryPt;
    double at = 0.0;
    for (const auto& entry : points) {
        const int16_t delay = entry.delay;
        const int16_t arg = entry.arg;
        if (delay > 0) {
            // Engine scales delays >= 4 by ticksPerUpdate/numBuffers/4.
            const int tpu = TicksPerUpdate();
            const int ticks = delay >= 4 ? (delay * tpu / (int)AudioContext::numBuffers) / 4 : delay;
            at += (double)ticks / (double)AdsrTickRate();
            const float lvl = (float)arg / 32767.0f;
            entryPt.push_back((int)ev.envPoints.size());
            ev.envPoints.emplace_back((float)at, lvl * lvl);
            if (ev.envPoints.size() >= 16) {
                break;
            }
        } else if (delay == -2) {
            if (arg >= 0 && arg < (int)entryPt.size() && entryPt[arg] >= 0) {
                ev.envLoopStartT = ev.envPoints[entryPt[arg]].first;
            }
            break;
        } else if (delay == -3) {
            ev.envLoopStartT = 0.0f;
            break;
        } else {
            break;
        }
        entryPt.push_back(-1);
    }
}

// Font envelopes store values byte-swapped (the port keeps envelopes
// big-endian in memory and swaps at use). Overrides (chan 0xDA, layer 0xCB)
// point into the sequence data itself.
void FillEnvelope(UI::SynthNote& ev, int envOverride, uint32_t envKey, const std::vector<uint8_t>& seq) {
    std::vector<EnvelopePoint> points;
    if (envOverride >= 0) {
        for (size_t at = (size_t)envOverride; at + 3 < seq.size() && points.size() < 24; at += 4) {
            const int16_t delay = (int16_t)((seq[at] << 8) | seq[at + 1]);
            const int16_t arg = (int16_t)((seq[at + 2] << 8) | seq[at + 3]);
            points.push_back({ delay, arg });
            if (delay <= 0) {
                break;
            }
        }
        FillEnvelopePoints(ev, points);
        return;
    }
    const auto eit = Assets().envByOffset.find(envKey);
    if (eit == Assets().envByOffset.end()) {
        return;
    }
    for (const auto& entry : eit->second->points) {
        points.push_back({ (int16_t)BSWAP16((uint16_t)entry.delay), (int16_t)BSWAP16((uint16_t)entry.arg) });
    }
    FillEnvelopePoints(ev, points);
}

float NoteFreq(int note) {
    return std::pow(2.0f, (float)(note - 39) / 12.0f);
}

struct M64Exec {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pc = 0;
    size_t stack[4]{};
    int sp = 0;
    struct {
        size_t addr;
        int count;
    } loops[4]{};
    int lp = 0;
    int delay = 0;
    bool active = false;
    bool failed = false;

    uint8_t U8() {
        if (pc >= size) {
            failed = true;
            return 0;
        }
        return data[pc++];
    }
    int8_t S8() {
        return (int8_t)U8();
    }
    uint16_t U16() {
        const uint16_t hi = U8();
        return (uint16_t)((hi << 8) | U8());
    }
    int Var() {
        int v = U8();
        if (v & 0x80) {
            v = ((v & 0x7F) << 8) | U8();
        }
        return v;
    }
    void Start(size_t addr) {
        pc = addr;
        sp = 0;
        lp = 0;
        delay = 0;
        active = addr < size;
        failed = false;
    }
    void End() {
        active = false;
    }
};

struct LayerSt {
    M64Exec ex;
    int releaseRate = -1; // layer 0xCB
    bool ignoreDrumPan = false;
    int transpose = 0;
    int noteDuration = 0x80;
    int lastDelay = 0;
    int shortVel = 0x50;
    int shortDur = 0;
    int shortDefaultDelay = 0;
    int portaMode = 0;
    int portaTarget = 0;
    int portaTime = 0;
    int envOverride = -1;
    bool continuous = false;
    int lastEvent = -1;
    double lastSlotEnd = 0.0;
    float lastVel = 1.0f;
    float pan = -1.0f;
    const InstrumentData* inst = nullptr;
    bool drums = false;
    bool instSet = false;
};

struct ChanSt {
    M64Exec ex;
    bool largeNotes = false;
    bool hasInst = false;
    bool drums = false;
    const InstrumentData* inst = nullptr;
    const FontRef* font = nullptr;
    float vol = 1.0f;
    float volScale = 1.0f;
    float freqScale = 1.0f;
    float pan = 0.5f;
    int transpose = 0;
    int8_t value = 0;
    int dynTable = -1;
    uint8_t vibExtent = 0;
    uint8_t vibExtentStart = 0;
    uint8_t vibRate = 0;
    uint8_t vibRateStart = 0;
    uint8_t vibRateRamp = 0;
    uint8_t vibDelay = 0;
    uint8_t vibRamp = 0;
    uint8_t reverb = 0;
    uint8_t decayIndex = 208;
    float sustain = 0.0f;   // fraction of gate-end level (0xD2 / 256)
    float panWeight = 1.0f; // channel share of the pan blend (0xDC / 128)
    int envOverride = -1;
    LayerSt layers[8];
};

struct SeqSt {
    M64Exec ex;
    float vol = 1.0f;
    int transpose = 0;
    int tempo = 120;
    int8_t value = 0;
    ChanSt chans[16];
};

struct SeqRenderer {
    const std::vector<uint8_t>& buf;
    const FontRef* defaultFont;
    // This sequence's font id list from the seq-font table (script font ops
    // index it in reverse: operand 0 = last entry).
    std::vector<uint8_t> seqFonts;
    SeqSt seq;
    std::vector<NoteEvent> events;
    double sec = 0.0;
    int8_t seqVariation = 0;
    int shortVelTable = -1;
    int shortDurTable = -1;
    UI::GainAutomation gainAuto[16];
    int pendingFadeTicks = 0;
    // Channel freq multiplier over time (0xD3/0xDE/0xEE bends on sounding notes).
    UI::PitchAutomation pitchAuto[16];

    SeqRenderer(const std::vector<uint8_t>& b, const FontRef* font) : buf(b), defaultFont(font) {
    }

    void PushPitch(int idx) {
        auto& a = pitchAuto[idx];
        const float f = seq.chans[idx].freqScale;
        if (a.empty() || a.back().second != f) {
            a.emplace_back((float)sec, f);
        }
    }

    int ReadU16At(int offset) const {
        if (offset < 0 || offset + 1 >= (int)buf.size()) {
            return -1;
        }
        return (buf[offset] << 8) | buf[offset + 1];
    }

    uint8_t ReadU8At(int offset) const {
        return offset >= 0 && offset < (int)buf.size() ? buf[offset] : 0;
    }

    void PushGain(int idx) {
        const ChanSt& ch = seq.chans[idx];
        // appliedVolume = SQ(channelVolume): the engine squares the whole
        // channel volume chain.
        float g = seq.vol * ch.vol * ch.volScale;
        g *= g;
        auto& a = gainAuto[idx];
        if (a.empty() || a.back().second != g) {
            a.emplace_back((float)sec, g);
        }
    }

    void PushGainAll() {
        for (int i = 0; i < 16; ++i) {
            if (seq.chans[i].ex.data != nullptr) {
                PushGain(i);
            }
        }
    }

    void StartChannel(int idx, uint16_t addr) {
        if (idx < 0 || idx >= 16) {
            return;
        }
        ChanSt& ch = seq.chans[idx];
        if (ch.ex.data == nullptr) {
            ch = ChanSt{};
            ch.font = defaultFont;
        }
        for (auto& ly : ch.layers) {
            ly = LayerSt{};
        }
        ch.value = 0;
        ch.ex.data = buf.data();
        ch.ex.size = buf.size();
        ch.ex.Start(addr);
        PushGain(idx);
        PushPitch(idx);
    }

    const FontRef* FontById(uint8_t id) {
        for (const auto& font : Assets().fonts) {
            if (font.id == id) {
                return &font;
            }
        }
        return nullptr;
    }

    const FontRef* FontByListIndex(uint8_t operand) {
        if (seqFonts.empty() || operand >= seqFonts.size()) {
            return nullptr;
        }
        return FontById(seqFonts[seqFonts.size() - 1 - operand]);
    }

    void SetChanInstrument(ChanSt& ch, uint8_t instId) {
        ch.hasInst = false;
        ch.drums = false;
        ch.inst = nullptr;
        if (instId >= 0x80) {
            return;
        }
        if (instId == 0x7F) {
            ch.drums = true;
            ch.hasInst = true;
            return;
        }
        if (ch.font != nullptr && instId < ch.font->insts.size() && ch.font->insts[instId] != nullptr) {
            ch.inst = ch.font->insts[instId];
            ch.decayIndex = (uint8_t)ch.inst->adsrDecayIndex;
            ch.hasInst = true;
        }
    }

    void EmitNote(ChanSt& ch, LayerSt& ly, int pitch, int vel, int delayTicks, int durByte) {
        if (!ch.hasInst && !ly.instSet) {
            return;
        }
        const bool drums = ly.instSet ? ly.drums : ch.drums;
        const InstrumentData* inst = ly.instSet ? ly.inst : ch.inst;
        const FontRef* font = ch.font;
        if (font == nullptr || events.size() >= kMaxEvents) {
            return;
        }

        NoteEvent ev{};
        ev.startSec = sec;
        const double secPerTick = 1.25 / (double)std::max(seq.tempo, 1);
        const int soundTicks = delayTicks - delayTicks * durByte / 256;
        ev.soundSec = std::max(soundTicks, 1) * secPerTick;
        const float velGain = ((float)vel / 127.0f) * ((float)vel / 127.0f);
        ev.gain = velGain;
        ev.chan = (int)(&ch - seq.chans);
        // notePan = chanPan*weight + layerPan*(1-weight); weight defaults to 1.
        const float layerPan = ly.pan >= 0.0f ? ly.pan : 0.5f;
        ev.pan = ch.pan * ch.panWeight + layerPan * (1.0f - ch.panWeight);
        ev.reverb = (float)ch.reverb / 127.0f;
        ev.releaseSec = ReleaseSeconds(ly.releaseRate >= 0 ? (uint8_t)ly.releaseRate : ch.decayIndex);
        ev.sustainLevel = ch.sustain;
        const float tick = AdsrTickRate();
        ev.sustainHoldSec = 128.0f / tick;
        ev.vibDepthStart = (float)ch.vibExtentStart / 127.0f;
        ev.vibDepthEnd = (float)ch.vibExtent / 127.0f;
        ev.vibDelaySec = (float)ch.vibDelay * 16.0f / tick;
        ev.vibRampSec = (float)ch.vibRamp * 16.0f / tick;
        ev.vibRateStartHz = (float)ch.vibRateStart / 16.0f * (tick / 240.0f);
        ev.vibRateEndHz = (float)ch.vibRate / 16.0f * (tick / 240.0f);
        ev.vibRateRampSec = (float)ch.vibRateRamp * 16.0f / tick;
        ev.portaRatio = 1.0f;
        ev.portaSec = 0.0f;

        auto& assets = Assets();
        if (drums) {
            if (font->drums.empty()) {
                return;
            }
            const int idx = std::clamp(pitch + ch.transpose + ly.transpose, 0, (int)font->drums.size() - 1);
            const DrumData* drum = font->drums[idx];
            if (drum == nullptr) {
                return;
            }
            ev.sample = SynthSampleFor(SampleByAddr(drum->tunedSample.sample));
            ev.freqScale = drum->tunedSample.tuning;
            if (!ly.ignoreDrumPan) {
                ev.pan = ch.pan * ch.panWeight + ((float)drum->pan / 128.0f) * (1.0f - ch.panWeight);
            }
            ev.releaseSec = ReleaseSeconds(drum->adsrDecayIndex);
            FillEnvelope(ev, ly.envOverride >= 0 ? ly.envOverride : ch.envOverride, drum->envelope, buf);
        } else {
            if (inst == nullptr) {
                return;
            }
            const int note = pitch + seq.transpose + ch.transpose + ly.transpose;
            if (note < 0 || note >= 0x80) {
                return;
            }
            const TunedSample* sound = &inst->normalPitchTunedSample;
            if (note < inst->normalRangeLo && inst->lowPitchTunedSample.sample != 0) {
                sound = &inst->lowPitchTunedSample;
            } else if (note > inst->normalRangeHi && inst->highPitchTunedSample.sample != 0) {
                sound = &inst->highPitchTunedSample;
            }
            ev.sample = SynthSampleFor(SampleByAddr(sound->sample));
            ev.freqScale = NoteFreq(note) * sound->tuning;
            FillEnvelope(ev, ly.envOverride >= 0 ? ly.envOverride : ch.envOverride, inst->envelope, buf);
            if (ly.portaMode != 0 && ly.portaTime > 0) {
                const int target = std::clamp(ly.portaTarget + seq.transpose + ch.transpose + ly.transpose, 0, 127);
                const float targetFreq = NoteFreq(target) * sound->tuning;
                if ((ly.portaMode & 1) != 0) {
                    ev.portaRatio = targetFreq / ev.freqScale;
                } else {
                    ev.portaRatio = ev.freqScale / targetFreq;
                    ev.freqScale = targetFreq;
                }
                ev.portaSec = (float)((ly.portaMode & 0x80) != 0 ? ev.soundSec * (double)ly.portaTime / 255.0
                                                                 : (double)ly.portaTime * secPerTick);
            }
        }
        if (ev.sample == nullptr || ev.freqScale <= 0.0f) {
            return;
        }
        if (ly.continuous && ly.lastEvent >= 0 && ly.lastEvent < (int)events.size()) {
            NoteEvent& prev = events[ly.lastEvent];
            if (prev.sample == ev.sample && std::fabs(ly.lastSlotEnd - sec) < secPerTick * 0.5 &&
                prev.segs.size() < 96) {
                prev.soundSec = (sec - prev.startSec) + ev.soundSec;
                NoteEvent::Seg seg;
                seg.t = (float)(sec - prev.startSec);
                seg.freq = ev.freqScale;
                seg.gainMul = ly.lastVel > 0.0001f ? velGain / ly.lastVel : 1.0f;
                prev.segs.push_back(seg);
                ly.lastSlotEnd = sec + delayTicks * secPerTick;
                return;
            }
        }
        ly.lastEvent = (int)events.size();
        ly.lastSlotEnd = sec + delayTicks * secPerTick;
        ly.lastVel = velGain;
        events.push_back(ev);
    }

    bool Flow(M64Exec& ex, uint8_t cmd, int8_t value) {
        switch (cmd) {
            case 0xFF:
                if (ex.sp > 0) {
                    ex.pc = ex.stack[--ex.sp];
                } else {
                    ex.End();
                }
                return true;
            case 0xFE:
                ex.delay = 1;
                return true;
            case 0xFD:
                ex.delay = std::max(ex.Var(), 1);
                return true;
            case 0xFC: {
                const uint16_t addr = ex.U16();
                if (ex.sp < 4) {
                    ex.stack[ex.sp++] = ex.pc;
                    ex.pc = addr;
                }
                return true;
            }
            case 0xFB:
                ex.pc = ex.U16();
                return true;
            case 0xFA: {
                const uint16_t addr = ex.U16();
                if (value == 0) {
                    ex.pc = addr;
                }
                return true;
            }
            case 0xF9: {
                const uint16_t addr = ex.U16();
                if (value < 0) {
                    ex.pc = addr;
                }
                return true;
            }
            case 0xF5: {
                const uint16_t addr = ex.U16();
                if (value >= 0) {
                    ex.pc = addr;
                }
                return true;
            }
            case 0xF8: {
                int count = ex.U8();
                if (count == 0) {
                    count = 256;
                }
                if (ex.lp < 4) {
                    ex.loops[ex.lp].addr = ex.pc;
                    ex.loops[ex.lp].count = count;
                    ex.lp++;
                }
                return true;
            }
            case 0xF7:
                if (ex.lp > 0) {
                    if (--ex.loops[ex.lp - 1].count > 0) {
                        ex.pc = ex.loops[ex.lp - 1].addr;
                    } else {
                        ex.lp--;
                    }
                }
                return true;
            case 0xF6:
                if (ex.lp > 0) {
                    ex.lp--;
                }
                return true;
            case 0xF4: {
                const int8_t rel = ex.S8();
                ex.pc += rel;
                return true;
            }
            case 0xF3: {
                const int8_t rel = ex.S8();
                if (value == 0) {
                    ex.pc += rel;
                }
                return true;
            }
            case 0xF2: {
                const int8_t rel = ex.S8();
                if (value < 0) {
                    ex.pc += rel;
                }
                return true;
            }
            default:
                return false;
        }
    }

    void StepSeq() {
        M64Exec& ex = seq.ex;
        int guard = 0;
        while (ex.active && !ex.failed && ex.delay == 0 && guard++ < 1000) {
            const uint8_t cmd = ex.U8();
            if (Flow(ex, cmd, seq.value)) {
                continue;
            }
            switch (cmd) {
                case 0xF1:
                    ex.U8();
                    break;
                case 0xF0:
                    break;
                case 0xDF:
                    seq.transpose = ex.S8();
                    break;
                case 0xDE:
                    seq.transpose += ex.S8();
                    break;
                case 0xDD:
                    seq.tempo = std::max<int>(ex.U8(), 1);
                    break;
                case 0xDC:
                    seq.tempo = std::max(seq.tempo + ex.S8(), 1);
                    break;
                case 0xDB: { // volume, honoring a pending timed fade
                    const float target = (float)ex.U8() / 127.0f;
                    if (pendingFadeTicks > 0) {
                        const double secPerTick = 1.25 / (double)std::max(seq.tempo, 1);
                        const float startVol = seq.vol;
                        const int steps = std::min(pendingFadeTicks, 64);
                        for (int i = 1; i <= steps; ++i) {
                            const float f = (float)i / (float)steps;
                            seq.vol = startVol + (target - startVol) * f;
                            const double at = sec + pendingFadeTicks * secPerTick * f;
                            for (int c = 0; c < 16; ++c) {
                                if (seq.chans[c].ex.data != nullptr) {
                                    const ChanSt& ch = seq.chans[c];
                                    const float cg = seq.vol * ch.vol * ch.volScale;
                                    gainAuto[c].emplace_back((float)at, cg * cg);
                                }
                            }
                        }
                        pendingFadeTicks = 0;
                    } else {
                        seq.vol = target;
                        PushGainAll();
                    }
                    break;
                }
                case 0xDA: // fade setup: mode + duration
                    ex.U8();
                    pendingFadeTicks = ex.U16();
                    break;
                case 0xD9:
                    ex.U8();
                    break;
                case 0xD7: {
                    const uint16_t mask = ex.U16();
                    for (int i = 0; i < 16; ++i) {
                        if ((mask >> i) & 1) {
                            seq.chans[i].ex.End();
                        }
                    }
                    break;
                }
                case 0xD6: {
                    const uint16_t mask = ex.U16();
                    for (int i = 0; i < 16; ++i) {
                        if ((mask >> i) & 1) {
                            seq.chans[i].ex.End();
                        }
                    }
                    break;
                }
                case 0xD5:
                    ex.S8();
                    break;
                case 0xD4:
                    break;
                case 0xD3:
                    ex.U8();
                    break;
                case 0xD2:
                    shortVelTable = ex.U16();
                    break;
                case 0xD1:
                    shortDurTable = ex.U16();
                    break;
                case 0xD0:
                    ex.U8();
                    break;
                case 0xCC:
                    seq.value = (int8_t)ex.U8();
                    break;
                case 0xC9:
                    seq.value = (int8_t)(seq.value & ex.U8());
                    break;
                case 0xC8:
                    seq.value = (int8_t)(seq.value - ex.U8());
                    break;
                case 0xC7:
                    ex.U8();
                    ex.U16();
                    break;
                case 0xC6:
                    ex.End();
                    break;
                default: {
                    if (cmd >= 0xC0) {
                        break;
                    }
                    const int lo = cmd & 0x0F;
                    switch (cmd & 0xF0) {
                        case 0x90:
                            StartChannel(lo, ex.U16());
                            break;
                        case 0x00:
                            seq.value = (int8_t)(lo < 16 && !seq.chans[lo].ex.active ? 1 : 0);
                            break;
                        case 0x50:
                            seq.value = (int8_t)(seq.value - seqVariation);
                            break;
                        case 0x70:
                            seqVariation = seq.value;
                            break;
                        case 0x80:
                            seq.value = seqVariation;
                            break;
                        default:
                            break;
                    }
                    break;
                }
            }
        }
    }

    void StepChan(ChanSt& ch) {
        M64Exec& ex = ch.ex;
        int guard = 0;
        while (ex.active && !ex.failed && ex.delay == 0 && guard++ < 1000) {
            const uint8_t cmd = ex.U8();
            if (Flow(ex, cmd, ch.value)) {
                continue;
            }
            switch (cmd) {
                case 0xF1:
                    ex.U8();
                    break;
                case 0xF0:
                    break;
                case 0xEA:
                    ex.End();
                    break;
                case 0xEB: { // setfontandinstr
                    const FontRef* font = FontByListIndex(ex.U8());
                    if (font != nullptr) {
                        ch.font = font;
                    }
                    SetChanInstrument(ch, ex.U8());
                    break;
                }
                case 0xC1:
                    SetChanInstrument(ch, ex.U8());
                    break;
                case 0xC2:
                    ch.dynTable = ex.U16();
                    break;
                case 0xC3:
                    ch.largeNotes = false;
                    break;
                case 0xC4:
                    ch.largeNotes = true;
                    break;
                case 0xC5:
                    if (ch.dynTable >= 0 && ch.value != -1) {
                        ch.dynTable = ReadU16At(ch.dynTable + 2 * (uint8_t)ch.value);
                    }
                    break;
                case 0xC6: { // setfont (reverse index into the seq's font list)
                    const FontRef* font = FontByListIndex(ex.U8());
                    if (font != nullptr) {
                        ch.font = font;
                    }
                    break;
                }
                case 0xC7:
                    ex.U8();
                    ex.U16();
                    break;
                case 0xC8:
                    ch.value = (int8_t)(ch.value - ex.U8());
                    break;
                case 0xC9:
                    ch.value = (int8_t)(ch.value & ex.U8());
                    break;
                case 0xCA:
                    ex.U8();
                    break;
                case 0xCB:
                    ex.U16();
                    break;
                case 0xCC:
                    ch.value = (int8_t)ex.U8();
                    break;
                case 0xCD: {
                    const uint8_t idx = ex.U8();
                    if (idx < 16) {
                        seq.chans[idx].ex.End();
                    }
                    break;
                }
                case 0xD0:
                case 0xD1:
                    ex.U8();
                    break;
                case 0xD2: // adsr sustain
                    ch.sustain = (float)ex.U8() / 256.0f;
                    break;
                case 0xD5:
                case 0xD6:
                    break;
                case 0xD3:
                    ch.freqScale = std::pow(2.0f, (float)ex.S8() / 127.0f);
                    PushPitch((int)(&ch - seq.chans));
                    break;
                case 0xD4:
                    ch.reverb = ex.U8();
                    break;
                case 0xD7:
                    ch.vibRate = ex.U8();
                    ch.vibRateStart = ch.vibRate;
                    ch.vibRateRamp = 0;
                    break;
                case 0xD8:
                    ch.vibExtent = ex.U8();
                    ch.vibExtentStart = ch.vibExtent;
                    ch.vibRamp = 0;
                    break;
                case 0xD9:
                    ch.decayIndex = ex.U8();
                    break;
                case 0xDA:
                    ch.envOverride = ex.U16();
                    break;
                case 0xDB:
                    ch.transpose = ex.S8();
                    break;
                case 0xDC: // pan channel weight
                    ch.panWeight = (float)ex.U8() / 128.0f;
                    break;
                case 0xDD:
                    ch.pan = (float)std::clamp<int>(ex.U8(), 0, 128) / 128.0f;
                    break;
                case 0xDE: // raw frequency multiplier N/2^15
                    ch.freqScale = (float)ex.U16() / 32768.0f;
                    PushPitch((int)(&ch - seq.chans));
                    break;
                case 0xDF:
                    ch.vol = (float)ex.U8() / 127.0f;
                    PushGain((int)(&ch - seq.chans));
                    break;
                case 0xE0:
                    ch.volScale = (float)ex.U8() / 128.0f;
                    PushGain((int)(&ch - seq.chans));
                    break;
                case 0xE1: // vibrato rate linear: start, target, delay
                    ch.vibRateStart = ex.U8();
                    ch.vibRate = ex.U8();
                    ch.vibRateRamp = ex.U8();
                    break;
                case 0xE2:
                    ch.vibExtentStart = ex.U8();
                    ch.vibExtent = ex.U8();
                    ch.vibRamp = ex.U8();
                    break;
                case 0xE3:
                    ch.vibDelay = ex.U8();
                    break;
                case 0xE5:
                case 0xE6:
                case 0xE9:
                case 0xED:
                    ex.U8();
                    break;
                case 0xEE: // pitch bend, +/- two semitones
                    ch.freqScale = std::pow(2.0f, (float)ex.S8() / 762.0f);
                    PushPitch((int)(&ch - seq.chans));
                    break;
                case 0xE4: {
                    if (ch.dynTable >= 0 && ch.value != -1) {
                        const int addr = ReadU16At(ch.dynTable + 2 * (uint8_t)ch.value);
                        if (addr >= 0 && ex.sp < 4) {
                            ex.stack[ex.sp++] = ex.pc;
                            ex.pc = (size_t)addr;
                        }
                    }
                    break;
                }
                case 0xEC: // vibrato + pitch reset
                    ch.vibExtent = 0;
                    ch.vibExtentStart = 0;
                    ch.vibRamp = 0;
                    ch.vibRate = 0;
                    ch.vibRateStart = 0;
                    ch.vibRateRamp = 0;
                    ch.freqScale = 1.0f;
                    PushPitch((int)(&ch - seq.chans));
                    break;
                case 0xE7:
                case 0xCE:
                case 0xCF:
                    ex.U16();
                    break;
                case 0xE8:
                    for (int i = 0; i < 8; ++i) {
                        ex.U8();
                    }
                    break;
                case 0xEF:
                    ex.U16();
                    ex.U8();
                    break;
                default: {
                    const int lo = cmd & 0x0F;
                    switch (cmd & 0xF0) {
                        case 0x00:
                            ch.value = (int8_t)(lo < 8 && !ch.layers[lo].ex.active ? 1 : 0);
                            break;
                        case 0x10: // sample load io
                            break;
                        case 0x20: // startchannel
                            StartChannel(lo, ex.U16());
                            break;
                        case 0x30:
                        case 0x40:
                            ex.U8();
                            break;
                        case 0x50:
                        case 0x70:
                        case 0x80:
                            break;
                        case 0x60: // delayshort
                            ex.delay = std::max(lo, 1);
                            break;
                        case 0x90: {
                            const uint16_t addr = ex.U16();
                            if (lo < 8) {
                                ch.layers[lo] = LayerSt{};
                                ch.layers[lo].ex.data = buf.data();
                                ch.layers[lo].ex.size = buf.size();
                                ch.layers[lo].ex.Start(addr);
                            }
                            break;
                        }
                        case 0xA0:
                            if (lo < 8) {
                                ch.layers[lo].ex.End();
                            }
                            break;
                        case 0xB0: {
                            if (ch.dynTable >= 0 && ch.value != -1 && lo < 8) {
                                const int addr = ReadU16At(ch.dynTable + 2 * (uint8_t)ch.value);
                                if (addr >= 0) {
                                    ch.layers[lo] = LayerSt{};
                                    ch.layers[lo].ex.data = buf.data();
                                    ch.layers[lo].ex.size = buf.size();
                                    ch.layers[lo].ex.Start((size_t)addr);
                                }
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
            }
        }
    }

    void StepLayer(ChanSt& ch, LayerSt& ly) {
        M64Exec& ex = ly.ex;
        int guard = 0;
        while (ex.active && !ex.failed && ex.delay == 0 && guard++ < 1000) {
            const uint8_t cmd = ex.U8();
            if (cmd >= 0xF2 && Flow(ex, cmd, 0)) {
                continue;
            }
            if (cmd >= 0xC0) {
                switch (cmd) {
                    case 0xC0:
                        ex.delay = std::max(ex.Var(), 1);
                        break;
                    case 0xC1:
                        ly.shortVel = ex.U8();
                        break;
                    case 0xC2:
                        ly.transpose = ex.S8();
                        break;
                    case 0xC3:
                        ly.shortDefaultDelay = ex.Var();
                        break;
                    case 0xC4:
                        ly.continuous = true;
                        break;
                    case 0xC5:
                        ly.continuous = false;
                        break;
                    case 0xC6: {
                        const uint8_t instId = ex.U8();
                        ly.instSet = true;
                        ly.drums = false;
                        ly.inst = nullptr;
                        if (instId == 0x7F) {
                            ly.drums = true;
                        } else if (instId < 0x80 && ch.font != nullptr && instId < ch.font->insts.size() &&
                                   ch.font->insts[instId] != nullptr) {
                            ly.inst = ch.font->insts[instId];
                            ly.releaseRate = ly.inst->adsrDecayIndex;
                        } else {
                            ly.instSet = false;
                        }
                        break;
                    }
                    case 0xC7: {
                        ly.portaMode = ex.U8();
                        ly.portaTarget = ex.U8();
                        if ((ly.portaMode & 0x80) != 0) {
                            ly.portaTime = ex.U8();
                        } else {
                            ly.portaTime = ex.Var();
                        }
                        break;
                    }
                    case 0xC8:
                        ly.portaMode = 0;
                        break;
                    case 0xC9: // shortnotegatetime
                        ly.shortDur = ex.U8();
                        break;
                    case 0xCA:
                        ly.pan = (float)std::clamp<int>(ex.U8(), 0, 128) / 128.0f;
                        break;
                    case 0xCB: // envelope override + decay
                        ly.envOverride = ex.U16();
                        ly.releaseRate = ex.U8();
                        break;
                    case 0xCC: // ignore drum pan
                        ly.ignoreDrumPan = true;
                        break;
                    case 0xCD:
                        ex.U8();
                        break;
                    default:
                        if ((cmd & 0xF0) == 0xD0) {
                            if (shortVelTable >= 0) {
                                ly.shortVel = ReadU8At(shortVelTable + (cmd & 0x0F));
                            }
                            break;
                        }
                        if ((cmd & 0xF0) == 0xE0) {
                            if (shortDurTable >= 0) {
                                ly.shortDur = ReadU8At(shortDurTable + (cmd & 0x0F));
                            }
                            break;
                        }
                        ex.failed = true;
                        break;
                }
                continue;
            }

            int pitch = cmd & 0x3F;
            int vel = 0x40;
            int delayTicks = 0;
            int durByte = ly.noteDuration;
            if (ch.largeNotes) {
                switch (cmd & 0xC0) {
                    case 0x00:
                        delayTicks = ex.Var();
                        vel = ex.U8();
                        durByte = ex.U8();
                        ly.lastDelay = delayTicks;
                        break;
                    case 0x40:
                        delayTicks = ex.Var();
                        vel = ex.U8();
                        durByte = 0;
                        ly.lastDelay = delayTicks;
                        break;
                    case 0x80:
                        delayTicks = ly.lastDelay;
                        vel = ex.U8();
                        durByte = ex.U8();
                        break;
                }
            } else {
                switch (cmd & 0xC0) {
                    case 0x00:
                        delayTicks = ex.Var();
                        ly.lastDelay = delayTicks;
                        break;
                    case 0x40:
                        delayTicks = ly.shortDefaultDelay;
                        break;
                    case 0x80:
                        delayTicks = ly.lastDelay;
                        break;
                }
                vel = ly.shortVel;
                durByte = ly.shortDur;
            }
            delayTicks = std::max(delayTicks, 1);
            EmitNote(ch, ly, pitch, std::clamp(vel, 0, 127), delayTicks, durByte);
            ex.delay = delayTicks;
        }
    }

    void Run() {
        seq.ex.data = buf.data();
        seq.ex.size = buf.size();
        seq.ex.Start(0);

        for (int tick = 0; tick < kMaxTicks && sec < UI::kSynthMaxSeconds; ++tick) {
            if (seq.ex.active) {
                if (seq.ex.delay > 0) {
                    seq.ex.delay--;
                }
                if (seq.ex.delay == 0) {
                    StepSeq();
                }
            }
            bool anyChan = false;
            for (auto& ch : seq.chans) {
                if (!ch.ex.active) {
                    continue;
                }
                anyChan = true;
                if (ch.ex.delay > 0) {
                    ch.ex.delay--;
                }
                if (ch.ex.delay == 0) {
                    StepChan(ch);
                }
                for (auto& ly : ch.layers) {
                    if (!ly.ex.active) {
                        continue;
                    }
                    if (ly.ex.delay > 0) {
                        ly.ex.delay--;
                    }
                    if (ly.ex.delay == 0) {
                        StepLayer(ch, ly);
                    }
                }
            }
            if (!seq.ex.active && !anyChan) {
                break;
            }
            sec += 1.25 / (double)std::max(seq.tempo, 1);
        }
    }
};

} // namespace

// Sequence id = position of this sequence's entry in the sequence table.
int SeqIdForItem(const ParseResultData& item) {
    auto it = AudioContext::tables.find(AudioTableType::SEQ_TABLE);
    if (it == AudioContext::tables.end() || it->second.info == nullptr) {
        return -1;
    }
    const uint32_t rel =
        GetSafeNode<uint32_t>(const_cast<ParseResultData&>(item).node, "offset", 0) - it->second.offset;
    const auto& entries = it->second.info->entries;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].addr == rel) {
            return (int)i;
        }
    }
    return -1;
}

// The seq-font table lists each sequence's font ids; the game's default
// font is the LAST entry (AudioLoad_GetFontsForSequence ends there).
std::vector<uint8_t> SeqFontsForItem(const ParseResultData& item) {
    const auto& blob = Assets().seqFontTable;
    const int seqId = SeqIdForItem(item);
    if (seqId < 0 || blob.size() < (size_t)(seqId * 2 + 2)) {
        return {};
    }
    const size_t at = ((size_t)blob[seqId * 2] << 8) | blob[seqId * 2 + 1];
    if (at >= blob.size() || blob[at] == 0 || at + blob[at] >= blob.size()) {
        return {};
    }
    return std::vector<uint8_t>(blob.begin() + at + 1, blob.begin() + at + 1 + blob[at]);
}

bool DecodeV1SampleToPcm(const ParseResultData& item, std::vector<int16_t>& pcm, int& rate) {
    if (!item.data.has_value()) {
        return false;
    }
    auto* sample = std::static_pointer_cast<NSampleData>(item.data.value()).get();
    const auto& assets = Assets();
    const auto tit = assets.refTuning.find(sample);
    if (tit != assets.refTuning.end()) {
        rate = (int)std::lround(tit->second * 32000.0f);
    } else if (sample->sampleRate != 0) {
        rate = (int)sample->sampleRate;
    } else {
        rate = (int)(32000.0f * sample->tuning);
    }
    if (rate <= 0) {
        rate = 32000;
    }

#ifdef SF64_SUPPORT
    if (AudioContext::driver == NAudioDrivers::SF64 && sample->codec == 2) {
        auto& table = AudioContext::tables[AudioTableType::SAMPLE_TABLE];
        const uint8_t* ptr = table.buffer.data() + table.info->entries[sample->sampleBankId].addr + sample->sampleAddr;
        std::vector<uint8_t> raw(ptr, ptr + sample->size);
        std::vector<int16_t> out(sample->size * 2, 0);
        SF64::DecompressAudio(raw, out.data());
        pcm.assign(out.begin(), out.begin() + sample->size);
        return !pcm.empty();
    }
#endif

    const auto lit = assets.loopByOffset.find(sample->loop);
    const auto bit = assets.bookByOffset.find(sample->book);
    if (lit == assets.loopByOffset.end() || bit == assets.bookByOffset.end()) {
        return false;
    }
    LUS::BinaryWriter aifc;
    AudioConverter::SampleV1ToAIFC(sample, lit->second, bit->second, aifc);
    const auto bytes = aifc.ToVector();
    aifc.Close();
    if (bytes.empty()) {
        return false;
    }
    LUS::BinaryWriter aiff;
    write_aiff(bytes, aiff);
    const bool ok = DecodeAiffBytes(aiff.ToVector(), pcm, rate);
    aiff.Close();
    return ok && !pcm.empty();
}

bool SequencePlayerV1::Render(const ParseResultData& item, int, UI::RenderedAudio& out) {
    auto data = std::static_pointer_cast<RawBuffer>(item.data.value());
    const auto& fonts = Assets().fonts;
    if (fonts.empty() || data->mBuffer.empty()) {
        return false;
    }
    const std::vector<uint8_t> seqFonts = SeqFontsForItem(item);
    const FontRef* font = nullptr;
    if (!seqFonts.empty()) {
        for (const auto& f : fonts) {
            if (f.id == seqFonts.back()) {
                font = &f;
                break;
            }
        }
    }
    if (font == nullptr) {
        SPDLOG_WARN("Sequence '{}': no seq-font table match, using font 0", item.name);
        font = &fonts.front();
    }

    SeqRenderer renderer(data->mBuffer, font);
    renderer.seqFonts = seqFonts;
    renderer.Run();
    SPDLOG_INFO("Sequence '{}': font {} ({} insts, {} drums), {} notes, {} fonts indexed, seqFontTable {} bytes",
                item.name, font->id, font->insts.size(), font->drums.size(), renderer.events.size(), fonts.size(),
                Assets().seqFontTable.size());
    if (renderer.events.empty()) {
        SPDLOG_WARN("Sequence '{}' rendered zero notes", item.name);
        return false;
    }
    double end = 0.0;
    for (const auto& ev : renderer.events) {
        end = std::max(end, ev.startSec + ev.soundSec + 0.5);
    }
    out.pcm = UI::Synthesize(renderer.events, renderer.gainAuto, renderer.pitchAuto, end);
    out.noteCount = renderer.events.size();
    return !out.pcm.empty();
}

#endif // BUILD_UI
