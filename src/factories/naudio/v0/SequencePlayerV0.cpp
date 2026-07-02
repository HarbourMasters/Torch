#ifdef BUILD_UI

#include "SequencePlayerV0.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <unordered_map>

#include "AudioManager.h"
#include "SampleFactory.h"
#include "SequenceFactory.h"
#include "spdlog/spdlog.h"

namespace {


constexpr int kMaxTicks = 60000;
constexpr size_t kMaxEvents = 60000;

using NoteEvent = UI::SynthNote;

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
    int8_t S8() { return (int8_t)U8(); }
    uint16_t U16() {
        const uint16_t hi = U8();
        return (uint16_t)((hi << 8) | U8());
    }
    int Var() { // m64_read_compressed_u16
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
    void End() { active = false; }
};

struct LayerSt {
    M64Exec ex;
    int transpose = 0;
    int noteDuration = 0x80;
    int lastDelay = 0;
    int shortVel = 0x50;
    int shortDur = 0;
    int shortDefaultDelay = 0;
    int portaMode = 0;
    int portaTarget = 0;
    int portaTime = 0;
    bool continuous = false;
    int lastEvent = -1;   // events index of the previous note on this layer
    double lastSlotEnd = 0.0;
    float lastVel = 1.0f;
    float pan = -1.0f; // <0 = inherit channel
    int envOverride = -1;  // EU layer 0xCB
    int releaseRate = -1;  // EU layer 0xCB
    bool ignoreDrumPan = false;
    const Instrument* inst = nullptr; // layer override
    bool drums = false;
    bool instSet = false;
};

struct ChanSt {
    M64Exec ex;
    bool largeNotes = false;
    bool hasInst = false;
    bool drums = false;
    const Instrument* inst = nullptr;
    const Bank* bank = nullptr;
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
    uint8_t releaseRate = 0x20;
    float sustain = 0.0f;   // fraction of gate-end level (0xD2 / 256)
    float panWeight = 1.0f; // channel share of the pan blend (0xDC / 128)
    int envOverride = -1; // bank envelope offset from 0xDA
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


float ReleaseSeconds(uint8_t rate) {
    if (rate == 0) {
        return 0.016f; // fadeOutVel = 0x8000/updatesPerFrame: gone in one frame
    }
    return 32768.0f / ((float)rate * 24.0f * 240.0f);
}

std::shared_ptr<UI::SynthSample> SynthSampleFor(AudioBankSample* sample) {
    static std::unordered_map<AudioBankSample*, std::shared_ptr<UI::SynthSample>> sCache;
    if (sample == nullptr) {
        return nullptr;
    }
    auto it = sCache.find(sample);
    if (it != sCache.end()) {
        return it->second;
    }
    auto synth = std::make_shared<UI::SynthSample>();
    int rate = 0;
    DecodeSampleToPcm(sample, synth->pcm, rate);
    synth->loopStart = sample->loop.start;
    synth->loopEnd = sample->loop.end;
    synth->looped = sample->loop.count != 0;
    return sCache.emplace(sample, std::move(synth)).first->second;
}

// Bank ADSR envelope to breakpoints: delay in 1/240s updates, level squared;
// ADSR_GOTO (-2) / ADSR_RESTART (-3) set the loop point.
void FillEnvelope(UI::SynthNote& ev, const Bank* bank, int envOverride, uint32_t envKey) {
    const uint32_t key = envOverride >= 0 ? (uint32_t)envOverride : envKey;
    const auto eit = bank->envelopes.find(key);
    if (eit == bank->envelopes.end()) {
        return;
    }
    std::vector<int> entryPt;
    double at = 0.0;
    for (const auto& entry : eit->second.entries) {
        if (entry.delay > 0) {
            at += (double)entry.delay / 240.0;
            const float lvl = (float)entry.arg / 32767.0f;
            entryPt.push_back((int)ev.envPoints.size());
            ev.envPoints.emplace_back((float)at, lvl * lvl);
            if (ev.envPoints.size() >= 16) {
                break;
            }
        } else if (entry.delay == -2) {
            if (entry.arg >= 0 && entry.arg < (int)entryPt.size() && entryPt[entry.arg] >= 0) {
                ev.envLoopStartT = ev.envPoints[entryPt[entry.arg]].first;
            }
            break;
        } else if (entry.delay == -3) {
            ev.envLoopStartT = 0.0f;
            break;
        } else {
            break;
        }
        entryPt.push_back(-1);
    }
}

float NoteFreq(int note) {
    return std::pow(2.0f, (float)(note - 39) / 12.0f);
}

AudioBankSample* SampleByOffset(const Bank* bank, uint32_t offset) {
    if (bank == nullptr) {
        return nullptr;
    }
    auto it = bank->samples.find(offset);
    return it != bank->samples.end() ? it->second : nullptr;
}

struct SeqRenderer {
    const std::vector<uint8_t>& buf;
    std::vector<const Bank*> banks;
    SeqSt seq;
    std::vector<NoteEvent> events;
    double sec = 0.0;
    int8_t seqVariation = 0;
    int shortVelTable = -1;
    int shortDurTable = -1;
    // MK64 ships the EU revision of the library: seq 0xDA/0xDB are fade
    // setup/target, channel 0x6N is delayshort, layers gain 0xCB/0xCC.
    bool euDialect = false;
    int pendingFadeTicks = 0;
    bool initChanMask[16]{};
    // Master*channel gain over time, so fades reach already-sounding notes.
    UI::GainAutomation gainAuto[16];
    // Channel freq multiplier over time (0xD3/0xDE bends on sounding notes).
    UI::PitchAutomation pitchAuto[16];

    void PushGain(int idx) {
        const ChanSt& ch = seq.chans[idx];
        const float g = seq.vol * ch.vol * ch.volScale;
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

    explicit SeqRenderer(const std::vector<uint8_t>& b) : buf(b) {}

    // Enabling a channel keeps its state (instrument, volume, pan, large-note
    // mode); only the script restarts and layers are freed.
    void StartChannel(int idx, uint16_t addr) {
        if (idx < 0 || idx >= 16) {
            return;
        }
        ChanSt& ch = seq.chans[idx];
        if (ch.ex.data == nullptr) {
            ch = ChanSt{};
            ch.bank = banks.empty() ? nullptr : banks[0];
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

    void EmitNote(ChanSt& ch, LayerSt& ly, int pitch, int vel, int delayTicks, int durByte) {
        if (!ch.hasInst && !ly.instSet) {
            return;
        }
        const bool drums = ly.instSet ? ly.drums : ch.drums;
        const Instrument* inst = ly.instSet ? ly.inst : ch.inst;
        const Bank* bank = ch.bank;
        if (bank == nullptr || events.size() >= kMaxEvents) {
            return;
        }

        NoteEvent ev{};
        ev.startSec = sec;
        const double secPerTick = 1.25 / (double)std::max(seq.tempo, 1);
        // The duration byte sets the release tail: the note decays once the
        // remaining delay reaches noteDuration * delay / 256.
        const int soundTicks = delayTicks - delayTicks * durByte / 256;
        ev.soundSec = std::max(soundTicks, 1) * secPerTick;
        const float velGain = ((float)vel / 127.0f) * ((float)vel / 127.0f);
        ev.gain = velGain;
        ev.chan = (int)(&ch - seq.chans);
        // notePan = chanPan*weight + layerPan*(1-weight); weight defaults to 1.
        const float layerPan = ly.pan >= 0.0f ? ly.pan : 0.5f;
        ev.pan = ch.pan * ch.panWeight + layerPan * (1.0f - ch.panWeight);
        ev.reverb = (float)ch.reverb / 127.0f;
        ev.releaseSec = ReleaseSeconds(ly.releaseRate >= 0 ? (uint8_t)ly.releaseRate : ch.releaseRate);
        ev.sustainLevel = ch.sustain;
        ev.vibDepthStart = (float)ch.vibExtentStart / 127.0f;
        ev.vibDepthEnd = (float)ch.vibExtent / 127.0f;
        ev.vibDelaySec = (float)ch.vibDelay * 16.0f / 240.0f;
        ev.vibRampSec = (float)ch.vibRamp * 16.0f / 240.0f;
        ev.vibRateStartHz = (float)ch.vibRateStart / 16.0f;
        ev.vibRateEndHz = (float)ch.vibRate / 16.0f;
        ev.vibRateRampSec = (float)ch.vibRateRamp * 16.0f / 240.0f;
        ev.portaRatio = 1.0f;
        ev.portaSec = 0.0f;

        if (drums) {
            const int idx = std::clamp(pitch + ch.transpose + ly.transpose, 0,
                                       bank->drums.empty() ? 0 : (int)bank->drums.size() - 1);
            if (bank->drums.empty()) {
                return;
            }
            const Drum& drum = bank->drums[idx];
            ev.sample = SynthSampleFor(SampleByOffset(bank, drum.sound.offset));
            ev.freqScale = drum.sound.tuning;
            if (!ly.ignoreDrumPan) {
                ev.pan = ch.pan * ch.panWeight + ((float)drum.pan / 128.0f) * (1.0f - ch.panWeight);
            }
            ev.releaseSec = ReleaseSeconds(drum.releaseRate);
            FillEnvelope(ev, bank, ly.envOverride >= 0 ? ly.envOverride : ch.envOverride, drum.envelope);
        } else {
            if (inst == nullptr) {
                return;
            }
            const int note = pitch + seq.transpose + ch.transpose + ly.transpose;
            if (note < 0 || note >= 0x80) {
                return;
            }
            const std::optional<AudioBankSound>* sound = &inst->soundMed;
            if (note < inst->normalRangeLo && inst->soundLo.has_value()) {
                sound = &inst->soundLo;
            } else if (note > inst->normalRangeHi && inst->soundHi.has_value()) {
                sound = &inst->soundHi;
            }
            if (!sound->has_value()) {
                return;
            }
            ev.sample = SynthSampleFor(SampleByOffset(bank, sound->value().offset));
            ev.freqScale = NoteFreq(note) * sound->value().tuning;
            FillEnvelope(ev, bank, ly.envOverride >= 0 ? ly.envOverride : ch.envOverride, inst->envelope);
            if (ly.portaMode != 0 && ly.portaTime > 0) {
                const int target = std::clamp(ly.portaTarget + seq.transpose + ch.transpose + ly.transpose, 0, 127);
                const float targetFreq = NoteFreq(target) * sound->value().tuning;
                // Odd modes slide note->target; even modes slide target->note.
                if ((ly.portaMode & 1) != 0) {
                    ev.portaRatio = targetFreq / ev.freqScale;
                } else {
                    ev.portaRatio = ev.freqScale / targetFreq;
                    ev.freqScale = targetFreq;
                }
                ev.portaSec = (float)((ly.portaMode & 0x80) != 0
                                          ? ev.soundSec * (double)ly.portaTime / 255.0
                                          : (double)ly.portaTime * secPerTick);
            }
        }
        if (ev.sample == nullptr || ev.freqScale <= 0.0f) {
            return;
        }
        // Legato: extend the previous note on this layer instead of retriggering
        // when the notes are adjacent and use the same sample.
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

    void SetChanInstrument(ChanSt& ch, uint8_t instId) {
        ch.hasInst = false;
        ch.drums = false;
        ch.inst = nullptr;
        if (instId >= 0x80) {
            return; // raw waves unsupported
        }
        if (instId == 0x7F) {
            ch.drums = true;
            ch.hasInst = true;
            return;
        }
        if (ch.bank != nullptr && instId < ch.bank->insts.size() && ch.bank->insts[instId].valid) {
            ch.inst = &ch.bank->insts[instId];
            ch.releaseRate = ch.inst->releaseRate;
            ch.hasInst = true;
        }
    }

    // Shared flow commands; returns true when handled.
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
                case 0xDB: {
                    const float target = (float)ex.U8() / 127.0f;
                    if (euDialect && pendingFadeTicks > 0) {
                        const double secPerTick = 1.25 / (double)std::max(seq.tempo, 1);
                        const float startVol = seq.vol;
                        const int steps = std::min(pendingFadeTicks, 64);
                        for (int i = 1; i <= steps; ++i) {
                            const float f = (float)i / (float)steps;
                            seq.vol = startVol + (target - startVol) * f;
                            const double at = sec + pendingFadeTicks * secPerTick * f;
                            for (int c = 0; c < 16; ++c) {
                                if (seq.chans[c].ex.data != nullptr) {
                                    const ChanSt& c2 = seq.chans[c];
                                    gainAuto[c].emplace_back((float)at, seq.vol * c2.vol * c2.volScale);
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
                case 0xDA:
                    if (euDialect) { // fade setup: mode + duration
                        ex.U8();
                        pendingFadeTicks = ex.U16();
                    } else { // changevol
                        seq.vol = std::clamp(seq.vol + (float)ex.S8() / 127.0f, 0.0f, 2.0f);
                        PushGainAll();
                    }
                    break;
                case 0xD9:
                    ex.U8();
                    break;
                case 0xD7: {
                    const uint16_t mask = ex.U16();
                    for (int i = 0; i < 16; ++i) {
                        initChanMask[i] = (mask >> i) & 1;
                        if (initChanMask[i]) {
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
                case 0xC7: // writeseq
                    ex.U8();
                    ex.U16();
                    break;
                case 0xC6:
                    ex.End();
                    break;
                default: {
                    if (cmd >= 0xC0) {
                        break; // the game skips undefined upper commands too
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
                            break; // remaining low commands take no arguments
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
                case 0xEB: { // setbankandinstr
                    const uint8_t idx = ex.U8();
                    if (idx < banks.size()) {
                        ch.bank = banks[idx];
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
                case 0xC5: // dynsetdyntable
                    if (ch.dynTable >= 0 && ch.value != -1) {
                        ch.dynTable = ReadU16At(ch.dynTable + 2 * (uint8_t)ch.value);
                    }
                    break;
                case 0xC6: {
                    const uint8_t idx = ex.U8();
                    if (idx < banks.size()) {
                        ch.bank = banks[idx];
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
                case 0xD0:
                case 0xD1:
                    ex.U8();
                    break;
                case 0xD2: // adsr sustain
                    ch.sustain = (float)ex.U8() / 256.0f;
                    break;
                case 0xD3: // pitch bend, +/- one octave
                    ch.freqScale = std::pow(2.0f, (float)ex.S8() / 127.0f);
                    PushPitch((int)(&ch - seq.chans));
                    break;
                case 0xD4:
                    ch.reverb = ex.U8();
                    break;
                case 0xD6:
                    ex.U8();
                    break;
                case 0xD9:
                    ch.releaseRate = ex.U8();
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
                case 0xE1: { // vibrato rate linear: start, target, delay
                    ch.vibRateStart = ex.U8();
                    ch.vibRate = ex.U8();
                    ch.vibRateRamp = ex.U8();
                    break;
                }
                case 0xE2: { // vibrato extent linear: start, target, delay
                    ch.vibExtentStart = ex.U8();
                    ch.vibExtent = ex.U8();
                    ch.vibRamp = ex.U8();
                    break;
                }
                case 0xE3:
                    ch.vibDelay = ex.U8();
                    break;
                case 0xE5:
                case 0xE6:
                case 0xE9:
                case 0xED:
                case 0xEE:
                    ex.U8();
                    break;
                case 0xE4: { // dyncall
                    if (ch.dynTable >= 0 && ch.value != -1) {
                        const int addr = ReadU16At(ch.dynTable + 2 * (uint8_t)ch.value);
                        if (addr >= 0 && ex.sp < 4) {
                            ex.stack[ex.sp++] = ex.pc;
                            ex.pc = (size_t)addr;
                        }
                    }
                    break;
                }
                case 0xEC:
                    break;
                case 0xE7:
                case 0xCE:
                case 0xCF:
                    ex.U16();
                    break;
                case 0xEF:
                    ex.U16();
                    ex.U8();
                    break;
                case 0xE8:
                    for (int i = 0; i < 8; ++i) {
                        ex.U8();
                    }
                    break;
                case 0xCD: { // disable channel by index
                    const uint8_t idx = ex.U8();
                    if (idx < 16) {
                        seq.chans[idx].ex.End();
                    }
                    break;
                }
                default: {
                    const int lo = cmd & 0x0F;
                    switch (cmd & 0xF0) {
                        case 0x00:
                            ch.value = (int8_t)(lo < 8 && !ch.layers[lo].ex.active ? 1 : 0);
                            break;
                        case 0x10:
                            StartChannel(lo, ex.U16());
                            break;
                        case 0x20:
                            if (lo < 16) {
                                seq.chans[lo].ex.End();
                            }
                            break;
                        case 0x30:
                        case 0x40:
                            ex.U8();
                            break;
                        case 0x50:
                        case 0x70:
                        case 0x80:
                            break;
                        case 0x60: // US: setnotepriority; EU: delayshort
                            if (euDialect) {
                                ex.delay = std::max(lo, 1);
                            }
                            break;
                        case 0xB0: { // dynsetlayer
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
                        default:
                            break; // remaining low commands take no arguments
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
                    case 0xC8:
                        ly.portaMode = 0;
                        break;
                    case 0xC6: {
                        const uint8_t instId = ex.U8();
                        ly.instSet = true;
                        ly.drums = false;
                        ly.inst = nullptr;
                        if (instId == 0x7F) {
                            ly.drums = true;
                        } else if (instId < 0x80 && ch.bank != nullptr && instId < ch.bank->insts.size() &&
                                   ch.bank->insts[instId].valid) {
                            ly.inst = &ch.bank->insts[instId];
                        } else {
                            ly.instSet = false;
                        }
                        break;
                    }
                    case 0xC7: { // portamento
                        ly.portaMode = ex.U8();
                        ly.portaTarget = ex.U8();
                        if ((ly.portaMode & 0x80) != 0) {
                            ly.portaTime = ex.U8();
                        } else {
                            ly.portaTime = ex.Var();
                        }
                        break;
                    }
                    case 0xC9:
                        ly.shortDur = ex.U8();
                        break;
                    case 0xCA:
                        ly.pan = (float)std::clamp<int>(ex.U8(), 0, 128) / 128.0f;
                        break;
                    case 0xCB: // EU: envelope + release rate
                        ly.envOverride = ex.U16();
                        ly.releaseRate = ex.U8();
                        break;
                    case 0xCC: // EU: ignore drum pan
                        ly.ignoreDrumPan = true;
                        break;
                    default:
                        if ((cmd & 0xF0) == 0xD0) { // velocity from seq table
                            if (shortVelTable >= 0) {
                                ly.shortVel = ReadU8At(shortVelTable + (cmd & 0x0F));
                            }
                            break;
                        }
                        if ((cmd & 0xF0) == 0xE0) { // duration from seq table
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

            // Note command.
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



const Bank* FindBankByName(const std::map<uint32_t, Bank>& banks, const std::string& name) {
    const auto lower = [](std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        return v;
    };
    const auto slash = name.find_last_of('/');
    const std::string base = lower(slash != std::string::npos ? name.substr(slash + 1) : name);
    for (const auto& [id, bank] : banks) {
        if (lower(bank.name) == base) {
            return &bank;
        }
    }
    // Names like "bank_2" carry the index as a trailing decimal number.
    size_t digits = base.size();
    while (digits > 0 && std::isdigit((unsigned char)base[digits - 1])) {
        digits--;
    }
    if (digits < base.size()) {
        const uint32_t idx = (uint32_t)std::stoul(base.substr(digits));
        const auto it = banks.find(idx);
        if (it != banks.end()) {
            return &it->second;
        }
    }
    for (const auto& [id, bank] : banks) {
        const std::string bn = lower(bank.name);
        if (base.find(bn) != std::string::npos || bn.find(base) != std::string::npos) {
            return &bank;
        }
    }
    return nullptr;
}

} // namespace

bool SequencePlayerV0::Render(const ParseResultData& item, int, UI::RenderedAudio& out) {
    auto data = std::static_pointer_cast<SequenceData>(item.data.value());
    if (AudioManager::Instance == nullptr) {
        return false;
    }
    static std::map<uint32_t, Bank> sBanks;
    if (sBanks.empty()) {
        sBanks = AudioManager::Instance->get_banks();
    }

    SeqRenderer renderer(data->mBuffer);
    renderer.euDialect = Companion::Instance->GetGameTitle().find("KART") != std::string::npos;
    for (const auto& bankName : data->mBanks) {
        const Bank* bank = FindBankByName(sBanks, bankName);
        if (bank != nullptr) {
            renderer.banks.push_back(bank);
        } else {
            SPDLOG_WARN("Sequence '{}': bank '{}' not found", item.name, bankName);
        }
    }
    if (renderer.banks.empty()) {
        return false;
    }

    renderer.Run();
    if (renderer.events.empty()) {
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
