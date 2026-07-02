#include "SequenceFactory.h"
#include "Companion.h"
#include <tinyxml2.h>

ExportResult SequenceBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto sequence = std::static_pointer_cast<SequenceData>(raw);

    WriteHeader(writer, Torch::ResourceType::Sequence, 0);
    writer.Write(sequence->mId);
    writer.Write(static_cast<uint32_t>(sequence->mBanks.size()));
    for (auto& bank : sequence->mBanks) {
        writer.Write(bank);
    }
    writer.Write(sequence->mSize);
    writer.Write(reinterpret_cast<char*>(sequence->mBuffer.data()), sequence->mSize);
    writer.Finish(write);
    return std::nullopt;
}

ExportResult SequenceModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto sequence = std::static_pointer_cast<SequenceData>(raw);
    *replacement += ".m64";
    writer.Write(reinterpret_cast<char*>(sequence->mBuffer.data()), sequence->mSize);
    writer.Finish(write);
    return std::nullopt;
}

ExportResult SequenceXMLExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    const auto sequence = std::static_pointer_cast<SequenceData>(raw);

    auto path = fs::path(*replacement);
    tinyxml2::XMLDocument seq;
    tinyxml2::XMLElement* root = seq.NewElement("Sequence");
    root->SetAttribute("ID", sequence->mId);
    root->SetAttribute("Path", (path.string() + "_data.m64").c_str());
    tinyxml2::XMLElement* banks = seq.NewElement("Banks");
    for (auto& bank : sequence->mBanks) {
        tinyxml2::XMLElement* entry = banks->InsertNewChildElement("Entry");
        entry->SetAttribute("Path", bank.c_str());
        banks->InsertEndChild(entry);
    }
    root->InsertEndChild(banks);
    seq.InsertEndChild(root);

    tinyxml2::XMLPrinter printer;
    seq.Accept(&printer);
    write.write(printer.CStr(), printer.CStrSize() - 1);

    auto data = (char*)sequence->mBuffer.data();
    std::vector<char> m64(data, data + sequence->mBuffer.size());
    Companion::Instance->RegisterCompanionFile(path.filename().string() + "_data.m64", m64);

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SequenceFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    auto id = data["id"].as<uint32_t>();
    auto size = data["size"].as<size_t>();
    const auto offset = data["offset"].as<size_t>();
    auto banks = data["banks"].as<std::vector<std::string>>();
    return std::make_shared<SequenceData>(id, size, buffer.data() + offset, banks);
}

std::optional<std::shared_ptr<IParsedData>> SequenceFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                           YAML::Node& node) {
    return std::make_shared<RawBuffer>(buffer.data(), buffer.size());
}
#ifdef BUILD_UI
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include <filesystem>

#include "SampleFactory.h"
#include "ui/BaseBackend.h"
#include "ui/Widgets.h"

// Reduced m64 interpreter (sequence/channel/layer scripts, US command set from
// sm64 seqplayer.c) feeding an offline synthesizer: notes pick key-ranged bank
// sounds, pitch = gNoteFrequencies[note] * tuning, simple attack/release
// envelope, per-channel pan/volume. Vibrato, portamento pitch slides, reverb
// and io conditionals are ignored.
namespace {

constexpr int kOutRate = 32000;
constexpr double kMaxSeconds = 150.0;
constexpr int kMaxTicks = 60000;
constexpr size_t kMaxEvents = 60000;

struct NoteEvent {
    double startSec;
    double soundSec;
    float freqScale;
    float gain; // velocity^2; channel/master gain comes from the automation timeline
    float pan;  // 0..1
    int chan;
    AudioBankSample* sample;
    const Envelope* envelope;
    float reverb; // 0..1 send
    float vibDelaySec;
    float vibRampSec;
    float vibDepthStart;
    float vibDepthEnd; // semitones
    float vibRateHz;
    float portaRatio; // end/start pitch ratio (1 = none)
    float portaSec;
    uint8_t releaseRate;
    // Legato continuations: at `t` after note start, switch pitch/velocity
    // without retriggering the voice.
    struct Seg {
        float t;
        float freq;
        float gainMul;
    };
    std::vector<Seg> segs;
};

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
    uint8_t vibDelay = 0;
    uint8_t vibRamp = 0;
    uint8_t reverb = 0;
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
    bool initChanMask[16]{};
    // Master*channel gain over time, so fades reach already-sounding notes.
    std::vector<std::pair<float, float>> gainAuto[16];

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
        ev.pan = ly.pan >= 0.0f ? ly.pan : ch.pan;
        ev.reverb = (float)ch.reverb / 127.0f;
        ev.vibDepthStart = (float)ch.vibExtentStart / 127.0f;
        ev.vibDepthEnd = (float)ch.vibExtent / 127.0f;
        ev.vibDelaySec = (float)ch.vibDelay * 16.0f / 240.0f;
        ev.vibRampSec = (float)ch.vibRamp * 16.0f / 240.0f;
        ev.vibRateHz = (float)ch.vibRate / 16.0f;
        ev.portaRatio = 1.0f;
        ev.portaSec = 0.0f;

        if (drums) {
            const int idx = std::clamp(pitch + ch.transpose + ly.transpose, 0,
                                       bank->drums.empty() ? 0 : (int)bank->drums.size() - 1);
            if (bank->drums.empty()) {
                return;
            }
            const Drum& drum = bank->drums[idx];
            ev.sample = SampleByOffset(bank, drum.sound.offset);
            ev.freqScale = drum.sound.tuning * ch.freqScale;
            ev.pan = (float)drum.pan / 128.0f;
            ev.releaseRate = drum.releaseRate;
            const uint32_t envKey = ch.envOverride >= 0 ? (uint32_t)ch.envOverride : drum.envelope;
            auto eit = bank->envelopes.find(envKey);
            ev.envelope = eit != bank->envelopes.end() ? &eit->second : nullptr;
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
            ev.sample = SampleByOffset(bank, sound->value().offset);
            ev.freqScale = NoteFreq(note) * sound->value().tuning * ch.freqScale;
            ev.releaseRate = inst->releaseRate;
            const uint32_t envKey = ch.envOverride >= 0 ? (uint32_t)ch.envOverride : inst->envelope;
            auto eit = bank->envelopes.find(envKey);
            ev.envelope = eit != bank->envelopes.end() ? &eit->second : nullptr;
            if (ly.portaMode != 0 && ly.portaTime > 0) {
                const int target = std::clamp(ly.portaTarget + seq.transpose + ch.transpose + ly.transpose, 0, 127);
                const float targetFreq = NoteFreq(target) * sound->value().tuning * ch.freqScale;
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
                case 0xDB:
                    seq.vol = (float)ex.U8() / 127.0f;
                    PushGainAll();
                    break;
                case 0xDA:
                    seq.vol = std::clamp(seq.vol + (float)ex.S8() / 127.0f, 0.0f, 2.0f);
                    PushGainAll();
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
                case 0xD2:
                    ex.U8();
                    break;
                case 0xD3: // pitch bend, +/- one octave
                    ch.freqScale = std::pow(2.0f, (float)ex.S8() / 127.0f);
                    break;
                case 0xD4:
                    ch.reverb = ex.U8();
                    break;
                case 0xD6:
                case 0xD9:
                    ex.U8();
                    break;
                case 0xD7:
                    ch.vibRate = ex.U8();
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
                case 0xDC:
                    ex.U8();
                    break;
                case 0xDD:
                    ch.pan = (float)std::clamp<int>(ex.U8(), 0, 128) / 128.0f;
                    break;
                case 0xDE: // raw frequency multiplier N/2^15
                    ch.freqScale = (float)ex.U16() / 32768.0f;
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
                    ex.U8();
                    ch.vibRate = ex.U8();
                    ex.U8();
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
                        case 0x60: // setnotepriority (priority in the low nibble)
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

        for (int tick = 0; tick < kMaxTicks && sec < kMaxSeconds; ++tick) {
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

// Renders the events into interleaved stereo PCM16 at kOutRate.
struct EnvCurve {
    struct Pt {
        float t;
        float lvl;
    };
    std::vector<Pt> pts;
    float loopStartT = -1.0f; // >=0: cycle [loopStartT, end]
};

EnvCurve BuildEnvCurve(const Envelope* env) {
    EnvCurve curve;
    if (env == nullptr) {
        return curve;
    }
    std::vector<int> entryPt;
    double at = 0.0;
    for (const auto& entry : env->entries) {
        if (entry.delay > 0) {
            at += (double)entry.delay / 240.0;
            const float lvl = (float)entry.arg / 32767.0f;
            entryPt.push_back((int)curve.pts.size());
            curve.pts.push_back({ (float)at, lvl * lvl });
            if (curve.pts.size() >= 16) {
                break;
            }
        } else if (entry.delay == -2) { // ADSR_GOTO
            const int target = entry.arg;
            if (target >= 0 && target < (int)entryPt.size()) {
                curve.loopStartT = curve.pts[entryPt[target]].t;
            }
            break;
        } else if (entry.delay == -3) { // ADSR_RESTART
            curve.loopStartT = 0.0f;
            break;
        } else { // HANG / DISABLE: hold the last level
            break;
        }
        entryPt.push_back(-1);
    }
    return curve;
}

float EvalEnv(const EnvCurve& curve, double t) {
    if (curve.pts.empty()) {
        return t < 0.002 ? (float)(t / 0.002) : 1.0f;
    }
    const float endT = curve.pts.back().t;
    if (t >= endT) {
        if (curve.loopStartT >= 0.0f && endT > curve.loopStartT + 0.0001f) {
            t = curve.loopStartT + std::fmod(t - curve.loopStartT, endT - curve.loopStartT);
        } else {
            return curve.pts.back().lvl;
        }
    }
    float prevT = 0.0f, prevL = 0.0f;
    for (const auto& pt : curve.pts) {
        if (t < pt.t) {
            const float span = pt.t - prevT;
            return span > 0.0001f ? prevL + (pt.lvl - prevL) * (float)((t - prevT) / span) : pt.lvl;
        }
        prevT = pt.t;
        prevL = pt.lvl;
    }
    return curve.pts.back().lvl;
}

float GainAt(const std::vector<std::pair<float, float>>& automation, float t) {
    float g = automation.empty() ? 1.0f : automation.front().second;
    for (const auto& [at, val] : automation) {
        if (at > t) {
            break;
        }
        g = val;
    }
    return g;
}

std::vector<int16_t> Synthesize(const std::vector<NoteEvent>& events,
                                const std::vector<std::pair<float, float>> (&gainAuto)[16], double lengthSec) {
    const size_t frames = (size_t)(std::min(lengthSec + 1.5, kMaxSeconds) * kOutRate);
    std::vector<float> mix(frames * 2, 0.0f);
    std::vector<float> wet(frames * 2, 0.0f);
    std::unordered_map<AudioBankSample*, std::vector<int16_t>> pcmCache;
    std::unordered_map<const Envelope*, EnvCurve> envCache;

    for (const auto& ev : events) {
        auto it = pcmCache.find(ev.sample);
        if (it == pcmCache.end()) {
            std::vector<int16_t> pcm;
            int rate = 0;
            DecodeSampleToPcm(ev.sample, pcm, rate);
            it = pcmCache.emplace(ev.sample, std::move(pcm)).first;
        }
        const std::vector<int16_t>& pcm = it->second;
        if (pcm.empty()) {
            continue;
        }
        auto eit = envCache.find(ev.envelope);
        if (eit == envCache.end()) {
            eit = envCache.emplace(ev.envelope, BuildEnvCurve(ev.envelope)).first;
        }
        const EnvCurve& envCurve = eit->second;

        const bool looped = ev.sample->loop.count != 0 && ev.sample->loop.end > ev.sample->loop.start &&
                            ev.sample->loop.end <= pcm.size();
        const double loopStart = ev.sample->loop.start;
        const double loopEnd = ev.sample->loop.end;

        const double releaseSec = 0.02 + (double)(255 - ev.releaseRate) / 255.0 * 0.35;
        const size_t start = (size_t)(ev.startSec * kOutRate);
        const size_t total = (size_t)((ev.soundSec + releaseSec) * kOutRate);
        const float panR = std::sqrt(std::clamp(ev.pan, 0.0f, 1.0f));
        const float panL = std::sqrt(1.0f - std::clamp(ev.pan, 0.0f, 1.0f));
        const double relTau = std::max(releaseSec * 0.35, 0.008);
        const auto& automation = gainAuto[std::clamp(ev.chan, 0, 15)];

        size_t seg = 0;
        float segFreq = ev.freqScale;
        float segGain = 1.0f;
        double pos = 0.0;
        for (size_t k = 0; k < total; ++k) {
            const size_t out = start + k;
            if (out >= frames) {
                break;
            }
            if (looped && pos >= loopEnd) {
                pos -= loopEnd - loopStart;
            }
            const size_t idx = (size_t)pos;
            if (idx + 1 >= pcm.size()) {
                break;
            }
            const double frac = pos - (double)idx;
            const float smp = (float)((1.0 - frac) * pcm[idx] + frac * pcm[idx + 1]);

            const double t = (double)k / kOutRate;
            while (seg < ev.segs.size() && t >= ev.segs[seg].t) {
                segFreq = ev.segs[seg].freq;
                segGain *= ev.segs[seg].gainMul;
                seg++;
            }

            float env = EvalEnv(envCurve, t);
            if (t > ev.soundSec) {
                env *= (float)std::exp(-(t - ev.soundSec) / relTau);
                if (env * ev.gain < 0.0015f) {
                    break;
                }
            }
            const float g = GainAt(automation, (float)(ev.startSec + t));
            const float v = smp * ev.gain * segGain * env * g;
            mix[out * 2] += v * panL;
            mix[out * 2 + 1] += v * panR;
            if (ev.reverb > 0.003f) {
                wet[out * 2] += v * panL * ev.reverb;
                wet[out * 2 + 1] += v * panR * ev.reverb;
            }

            double step = segFreq;
            if (ev.portaSec > 0.0f && seg == 0) {
                step *= std::pow((double)ev.portaRatio, std::min(t / ev.portaSec, 1.0));
            }
            if (ev.vibRateHz > 0.1f && t > ev.vibDelaySec) {
                const double vt = t - ev.vibDelaySec;
                const float depth =
                    ev.vibRampSec > 0.001f
                        ? ev.vibDepthStart + (ev.vibDepthEnd - ev.vibDepthStart) *
                                                 (float)std::min(vt / ev.vibRampSec, 1.0)
                        : ev.vibDepthEnd;
                if (depth > 0.001f) {
                    step *= std::pow(2.0, depth * std::sin(6.2831853 * ev.vibRateHz * vt) / 12.0);
                }
            }
            pos += step;
        }
    }

    // Reverb: two feedback combs per side on the send bus.
    if (frames > 0) {
        const int d1 = (int)(0.043 * kOutRate);
        const int d2 = (int)(0.061 * kOutRate);
        for (int side = 0; side < 2; ++side) {
            for (size_t i = 0; i < frames; ++i) {
                float acc = wet[i * 2 + side];
                if ((int)i >= d1) {
                    acc += 0.42f * wet[(i - d1) * 2 + side];
                }
                if ((int)i >= d2) {
                    acc += 0.33f * wet[(i - d2) * 2 + side];
                }
                wet[i * 2 + side] = acc;
                mix[i * 2 + side] += acc * 0.55f;
            }
        }
    }

    // Normalize against clipping.
    float peak = 1.0f;
    for (const float v : mix) {
        peak = std::max(peak, std::fabs(v) / 30000.0f);
    }
    std::vector<int16_t> out(mix.size());
    for (size_t i = 0; i < mix.size(); ++i) {
        out[i] = (int16_t)std::clamp(mix[i] / peak, -32000.0f, 32000.0f);
    }
    return out;
}

struct RenderedSeq {
    std::string name;
    std::vector<int16_t> pcm;
    size_t noteCount = 0;
    double seconds = 0.0;
};
RenderedSeq sRendered;
std::string sPlayingSeq;

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
    for (const auto& [id, bank] : banks) {
        const std::string bn = lower(bank.name);
        if (base.find(bn) != std::string::npos || bn.find(base) != std::string::npos) {
            return &bank;
        }
    }
    return nullptr;
}

bool RenderSequence(const ParseResultData& item) {
    if (sRendered.name == item.name) {
        return !sRendered.pcm.empty();
    }
    sRendered = {};
    sRendered.name = item.name;

    auto data = std::static_pointer_cast<SequenceData>(item.data.value());
    if (AudioManager::Instance == nullptr) {
        return false;
    }
    static std::map<uint32_t, Bank> sBanks;
    if (sBanks.empty()) {
        sBanks = AudioManager::Instance->get_banks();
    }

    {
        std::error_code ec;
        std::filesystem::create_directories("/tmp/torch-seq-debug", ec);
        std::string base = item.name;
        std::replace(base.begin(), base.end(), '/', '_');
        FILE* f = fopen(("/tmp/torch-seq-debug/" + base + ".m64").c_str(), "wb");
        if (f != nullptr) {
            fwrite(data->mBuffer.data(), 1, data->mBuffer.size(), f);
            fclose(f);
        }
    }

    SeqRenderer renderer(data->mBuffer);
    for (const auto& bankName : data->mBanks) {
        const Bank* bank = FindBankByName(sBanks, bankName);
        if (bank != nullptr) {
            renderer.banks.push_back(bank);
        } else {
            fprintf(stderr, "[seq] '%s' bank '%s' not found (have:", item.name.c_str(), bankName.c_str());
            for (const auto& [id, b] : sBanks) {
                fprintf(stderr, " %s", b.name.c_str());
            }
            fprintf(stderr, ")\n");
        }
    }
    if (renderer.banks.empty()) {
        fprintf(stderr, "[seq] '%s' no banks resolved\n", item.name.c_str());
        return false;
    }

    renderer.Run();
    {
        int chans = 0, large = 0;
        for (const auto& ch : renderer.seq.chans) {
            if (ch.ex.data != nullptr && ch.ex.pc != 0) {
                chans++;
                if (ch.largeNotes) {
                    large++;
                }
            }
        }
        fprintf(stderr, "[seq] '%s' tempo=%d notes=%zu secs=%.1f chans=%d large=%d\n", item.name.c_str(),
                renderer.seq.tempo, renderer.events.size(), renderer.sec, chans, large);
    }
    if (renderer.events.empty()) {
        fprintf(stderr, "[seq] '%s' rendered zero notes\n", item.name.c_str());
        return false;
    }
    double end = 0.0;
    for (const auto& ev : renderer.events) {
        end = std::max(end, ev.startSec + ev.soundSec + 0.5);
    }
    sRendered.pcm = Synthesize(renderer.events, renderer.gainAuto, end);
    sRendered.noteCount = renderer.events.size();
    sRendered.seconds = (double)sRendered.pcm.size() / 2.0 / kOutRate;
    return !sRendered.pcm.empty();
}

} // namespace

float SequenceFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetFrameHeightWithSpacing() * 2.0f +
           ImGui::GetStyle().ItemSpacing.y * 4.0f;
}

void SequenceFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    auto data = std::static_pointer_cast<SequenceData>(item.data.value());
    std::string bankList;
    for (const auto& bank : data->mBanks) {
        if (!bankList.empty()) {
            bankList += ", ";
        }
        bankList += bank;
    }
    ImGui::TextDisabled("sequence  \xe2\x80\x94  %u bytes, banks: %s", data->mSize,
                        bankList.empty() ? "none" : bankList.c_str());

    const bool playingThis = sPlayingSeq == item.name && UI::GetBackend()->AudioProgress() >= 0.0f;
    if (ImGui::Button(playingThis ? "Stop##seq" : "Play##seq")) {
        if (playingThis) {
            UI::GetBackend()->StopAudio();
            sPlayingSeq.clear();
        } else if (RenderSequence(item)) {
            UI::GetBackend()->SetAudioSpeed(1.0f);
            if (UI::GetBackend()->PlaySamples(sRendered.pcm.data(), sRendered.pcm.size() / 2, kOutRate, 2)) {
                sPlayingSeq = item.name;
            }
        }
    }
    ImGui::SameLine();
    float volume = UI::GetBackend()->GetAudioVolume();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderFloat("##seqvol", &volume, 0.0f, 1.0f, "vol %.2f")) {
        UI::GetBackend()->SetAudioVolume(volume);
    }
    ImGui::SameLine();
    if (sRendered.name == item.name && !sRendered.pcm.empty()) {
        ImGui::TextDisabled("%zu notes, %.1fs", sRendered.noteCount, sRendered.seconds);
    } else {
        ImGui::TextDisabled("press play to render");
    }

    float progress = playingThis ? std::max(UI::GetBackend()->AudioProgress(), 0.0f) : 0.0f;
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 420.0f));
    if (ImGui::SliderFloat("##seqseek", &progress, 0.0f, 1.0f, "") && playingThis) {
        UI::GetBackend()->SeekAudio(progress);
    }
}
#endif // BUILD_UI
