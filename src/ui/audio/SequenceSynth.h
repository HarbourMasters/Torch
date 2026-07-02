#pragma once

#ifdef BUILD_UI

#include <cstdint>
#include <memory>
#include <vector>

// Game-agnostic offline sequence synthesizer. Drivers interpret their own
// sequence format into SynthNotes referencing decoded samples, plus per-channel
// gain automation; Synthesize mixes them into stereo PCM.
namespace UI {

constexpr int kSynthRate = 32000;
constexpr double kSynthMaxSeconds = 330.0;

struct SynthSample {
    std::vector<int16_t> pcm; // decoded, 1:1 with the source sample units
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    bool looped = false;
};

struct SynthNote {
    double startSec = 0.0;
    double soundSec = 0.0; // sounding time before the release begins
    float freqScale = 1.0f;
    float gain = 1.0f; // velocity term; channel gain comes from automation
    float pan = 0.5f;  // 0..1
    int chan = 0;
    std::shared_ptr<SynthSample> sample;
    float reverb = 0.0f;

    // Envelope as (time sec, level 0..1) breakpoints; loopStartT >= 0 cycles
    // the tail. Empty = default fast attack + hold.
    std::vector<std::pair<float, float>> envPoints;
    float envLoopStartT = -1.0f;

    // Release is the game's linear fade: full scale to zero in releaseSec.
    // sustainLevel (0..1) floors the fade at that fraction of the gate-end
    // level, holds ~0.53s, then resumes.
    float releaseSec = 0.1f;
    float sustainLevel = 0.0f;
    float sustainHoldSec = 128.0f / 240.0f;
    float vibDelaySec = 0.0f;
    float vibRampSec = 0.0f;
    float vibDepthStart = 0.0f;
    float vibDepthEnd = 0.0f; // semitones
    float vibRateStartHz = 0.0f;
    float vibRateEndHz = 0.0f;
    float vibRateRampSec = 0.0f;
    float portaRatio = 1.0f; // end/start pitch ratio (1 = none)
    float portaSec = 0.0f;

    // Legato continuations: pitch/velocity switches without retriggering.
    struct Seg {
        float t;
        float freq;
        float gainMul;
    };
    std::vector<Seg> segs;
};

// Per-channel (time sec, value) points; applied to notes while they sound.
// Gain tracks channel/master volume, pitch tracks the channel freq multiplier.
using GainAutomation = std::vector<std::pair<float, float>>;
using PitchAutomation = std::vector<std::pair<float, float>>;

struct RenderedAudio {
    std::vector<int16_t> pcm; // interleaved stereo at kSynthRate
    size_t noteCount = 0;
    double seconds = 0.0;
};

std::vector<int16_t> Synthesize(const std::vector<SynthNote>& notes, const GainAutomation (&gainAuto)[16],
                                const PitchAutomation (&pitchAuto)[16], double lengthSec);

} // namespace UI

#endif // BUILD_UI
