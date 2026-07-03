#ifdef BUILD_UI

#include "SequenceSynth.h"

#include <algorithm>
#include <cmath>

namespace UI {
namespace {

float EvalEnv(const SynthNote& note, double t) {
    const auto& pts = note.envPoints;
    if (pts.empty()) {
        return t < 0.002 ? (float)(t / 0.002) : 1.0f;
    }
    const float endT = pts.back().first;
    if (t >= endT) {
        if (note.envLoopStartT >= 0.0f && endT > note.envLoopStartT + 0.0001f) {
            t = note.envLoopStartT + std::fmod(t - note.envLoopStartT, endT - note.envLoopStartT);
        } else {
            return pts.back().second;
        }
    }
    float prevT = 0.0f, prevL = 0.0f;
    for (const auto& [pt, lvl] : pts) {
        if (t < pt) {
            const float span = pt - prevT;
            return span > 0.0001f ? prevL + (lvl - prevL) * (float)((t - prevT) / span) : lvl;
        }
        prevT = pt;
        prevL = lvl;
    }
    return pts.back().second;
}

// Step-wise automation lookup with a monotonic cursor.
struct AutoCursor {
    const std::vector<std::pair<float, float>>* track = nullptr;
    size_t idx = 0;
    float value = 1.0f;

    void Init(const std::vector<std::pair<float, float>>& t, float startT) {
        track = &t;
        idx = 0;
        value = t.empty() ? 1.0f : t.front().second;
        Advance(startT);
    }
    void Advance(float t) {
        while (track != nullptr && idx < track->size() && (*track)[idx].first <= t) {
            value = (*track)[idx].second;
            idx++;
        }
    }
};

} // namespace

std::vector<int16_t> Synthesize(const std::vector<SynthNote>& notes, const GainAutomation (&gainAuto)[16],
                                const PitchAutomation (&pitchAuto)[16], double lengthSec) {
    const size_t frames = (size_t)(std::min(lengthSec + 1.5, kSynthMaxSeconds) * kSynthRate);
    std::vector<float> mix(frames * 2, 0.0f);
    std::vector<float> wet(frames * 2, 0.0f);

    for (const auto& note : notes) {
        if (note.sample == nullptr || note.sample->pcm.empty()) {
            continue;
        }
        const std::vector<int16_t>& pcm = note.sample->pcm;
        const bool looped =
            note.sample->looped && note.sample->loopEnd > note.sample->loopStart && note.sample->loopEnd <= pcm.size();
        const double loopStart = note.sample->loopStart;
        const double loopEnd = note.sample->loopEnd;

        const size_t start = (size_t)(note.startSec * kSynthRate);
        const float panR = std::sqrt(std::clamp(note.pan, 0.0f, 1.0f));
        const float panL = std::sqrt(1.0f - std::clamp(note.pan, 0.0f, 1.0f));
        const int chan = std::clamp(note.chan, 0, 15);

        AutoCursor gainCur, pitchCur;
        gainCur.Init(gainAuto[chan], (float)note.startSec);
        pitchCur.Init(pitchAuto[chan], (float)note.startSec);

        // Release: linear at full-scale/releaseSec, optional sustain plateau.
        const float relSlope = 1.0f / std::max(note.releaseSec, 0.001f);
        float gateLevel = -1.0f;
        float susLevel = 0.0f;
        double susHoldEnd = 0.0;

        // Release + sustain can extend well past the gate; bound generously.
        const size_t total = (size_t)((note.soundSec + note.releaseSec + 1.0) * kSynthRate);

        size_t seg = 0;
        float segFreq = note.freqScale;
        float segGain = 1.0f;
        double pos = 0.0;
        double vibPhase = 0.0;
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

            const double t = (double)k / kSynthRate;
            while (seg < note.segs.size() && t >= note.segs[seg].t) {
                segFreq = note.segs[seg].freq;
                segGain *= note.segs[seg].gainMul;
                seg++;
            }

            float env;
            if (t <= note.soundSec) {
                env = EvalEnv(note, t);
            } else {
                if (gateLevel < 0.0f) {
                    gateLevel = EvalEnv(note, note.soundSec);
                    susLevel = note.sustainLevel > 0.001f ? gateLevel * note.sustainLevel : 0.0f;
                    if (susLevel > 0.0f) {
                        // Time to fade to the sustain floor, then the engine's hold.
                        susHoldEnd = note.soundSec + (gateLevel - susLevel) / relSlope + note.sustainHoldSec;
                    }
                }
                const double rel = t - note.soundSec;
                env = gateLevel - (float)rel * relSlope;
                if (susLevel > 0.0f) {
                    if (t <= susHoldEnd) {
                        env = std::max(env, susLevel);
                    } else {
                        env = susLevel - (float)(t - susHoldEnd) * relSlope;
                    }
                }
                if (env <= 0.0f) {
                    break;
                }
            }

            gainCur.Advance((float)(note.startSec + t));
            pitchCur.Advance((float)(note.startSec + t));
            const float v = smp * note.gain * segGain * env * gainCur.value;
            mix[out * 2] += v * panL;
            mix[out * 2 + 1] += v * panR;
            if (note.reverb > 0.003f) {
                wet[out * 2] += v * panL * note.reverb;
                wet[out * 2 + 1] += v * panR * note.reverb;
            }

            double step = (double)segFreq * pitchCur.value;
            if (note.portaSec > 0.0f && seg == 0) {
                step *= std::pow((double)note.portaRatio, std::min(t / note.portaSec, 1.0));
            }
            if ((note.vibRateEndHz > 0.1f || note.vibRateStartHz > 0.1f) && t > note.vibDelaySec) {
                const double vt = t - note.vibDelaySec;
                const float rate = note.vibRateRampSec > 0.001f
                                       ? note.vibRateStartHz + (note.vibRateEndHz - note.vibRateStartHz) *
                                                                   (float)std::min(vt / note.vibRateRampSec, 1.0)
                                       : note.vibRateEndHz;
                vibPhase += 6.2831853 * rate / kSynthRate;
                const float depth = note.vibRampSec > 0.001f
                                        ? note.vibDepthStart + (note.vibDepthEnd - note.vibDepthStart) *
                                                                   (float)std::min(vt / note.vibRampSec, 1.0)
                                        : note.vibDepthEnd;
                if (depth > 0.001f) {
                    step *= std::pow(2.0, depth * std::sin(vibPhase) / 12.0);
                }
            }
            pos += step;
        }
    }

    // Two feedback combs per side on the reverb send.
    if (frames > 0) {
        const int d1 = (int)(0.043 * kSynthRate);
        const int d2 = (int)(0.061 * kSynthRate);
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

} // namespace UI

#endif // BUILD_UI
