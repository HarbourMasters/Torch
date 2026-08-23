#include "VadpcmEncode.h"

#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace BK64 {
namespace {

constexpr int kFrameSamples = 16;
constexpr int kOrder = 2;

int16_t Clamp16(int32_t value) {
    return static_cast<int16_t>(value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
}

bool EndsWith(const std::string& text, const char* suffix) {
    const size_t length = std::strlen(suffix);
    if (text.size() < length) {
        return false;
    }
    const char* at = text.c_str() + text.size() - length;
    for (size_t i = 0; i < length; i++) {
        const char left = static_cast<char>(std::tolower(static_cast<unsigned char>(at[i])));
        const char right = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (left != right) {
            return false;
        }
    }
    return true;
}

std::vector<int16_t> ToMonoAtRate(const float* interleaved, uint64_t frames, uint32_t channels, uint32_t sourceRate,
                                  int rate) {
    std::vector<float> mono(static_cast<size_t>(frames));
    for (uint64_t i = 0; i < frames; i++) {
        float sum = 0.0f;
        for (uint32_t c = 0; c < channels; c++) {
            sum += interleaved[i * channels + c];
        }
        mono[static_cast<size_t>(i)] = sum / static_cast<float>(channels);
    }

    if (sourceRate == static_cast<uint32_t>(rate) || mono.empty()) {
        std::vector<int16_t> out(mono.size());
        for (size_t i = 0; i < mono.size(); i++) {
            out[i] = Clamp16(static_cast<int32_t>(mono[i] * 32767.0f));
        }
        return out;
    }

    const double ratio = static_cast<double>(sourceRate) / static_cast<double>(rate);
    const size_t count = static_cast<size_t>(static_cast<double>(mono.size()) / ratio);
    std::vector<int16_t> out(count);
    for (size_t i = 0; i < count; i++) {
        const double at = static_cast<double>(i) * ratio;
        const size_t idx = static_cast<size_t>(at);
        const double frac = at - static_cast<double>(idx);
        const float first = mono[std::min(idx, mono.size() - 1)];
        const float second = mono[std::min(idx + 1, mono.size() - 1)];
        out[i] = Clamp16(static_cast<int32_t>((first + (second - first) * static_cast<float>(frac)) * 32767.0f));
    }
    return out;
}

void FitOrder2(const std::vector<int16_t>& pcm, double& a1, double& a2) {
    double r[3] = { 0.0, 0.0, 0.0 };
    for (int lag = 0; lag <= kOrder; lag++) {
        double sum = 0.0;
        for (size_t i = lag; i < pcm.size(); i++) {
            sum += static_cast<double>(pcm[i]) * static_cast<double>(pcm[i - lag]);
        }
        r[lag] = sum;
    }
    if (r[0] <= 0.0) {
        a1 = 0.0;
        a2 = 0.0;
        return;
    }
    r[0] *= 1.0001;

    const double k1 = r[1] / r[0];
    double err = r[0] * (1.0 - k1 * k1);
    if (err <= 0.0) {
        a1 = k1;
        a2 = 0.0;
        return;
    }
    const double k2 = (r[2] - k1 * r[1]) / err;
    a1 = k1 - k2 * k1;
    a2 = k2;
}

void BuildBookRows(double a1, double a2, int16_t* row0, int16_t* row1) {
    double p0 = 1.0, p1 = 0.0;
    double q0 = 0.0, q1 = 1.0;
    for (int i = 0; i < 8; i++) {
        const double n0 = a1 * p1 + a2 * p0;
        const double n1 = a1 * q1 + a2 * q0;
        p0 = p1;
        p1 = n0;
        q0 = q1;
        q1 = n1;
        row0[i] = Clamp16(static_cast<int32_t>(std::lround(p1 * 2048.0)));
        row1[i] = Clamp16(static_cast<int32_t>(std::lround(q1 * 2048.0)));
    }
}

void DecodeFrame(const uint8_t* frame, const int16_t* tbl0, const int16_t* tbl1, int16_t* out) {
    const int shift = frame[0] >> 4;
    const uint8_t* nibbles = frame + 1;
    for (int half = 0; half < 2; half++) {
        int16_t ins[8];
        const int16_t prev1 = out[-1];
        const int16_t prev2 = out[-2];
        for (int j = 0; j < 4; j++) {
            ins[j * 2] = static_cast<int16_t>((((*nibbles >> 4) << 28) >> 28) << shift);
            ins[j * 2 + 1] = static_cast<int16_t>((((*nibbles++ & 0xF) << 28) >> 28) << shift);
        }
        for (int j = 0; j < 8; j++) {
            int32_t acc = tbl0[j] * prev2 + tbl1[j] * prev1 + (ins[j] << 11);
            for (int k = 0; k < j; k++) {
                acc += tbl1[(j - k) - 1] * ins[k];
            }
            *out++ = Clamp16(acc >> 11);
        }
    }
}

int64_t TryShift(const int16_t* want, const int16_t* history, const int16_t* tbl0, const int16_t* tbl1, int shift,
                 int predictor, uint8_t* frame, int16_t* got) {
    std::memset(frame, 0, 9);
    frame[0] = static_cast<uint8_t>((shift << 4) | (predictor & 0xF));

    int16_t work[18];
    work[0] = history[0];
    work[1] = history[1];

    for (int half = 0; half < 2; half++) {
        int16_t* out = work + 2 + half * 8;
        const int16_t prev1 = out[-1];
        const int16_t prev2 = out[-2];
        int16_t ins[8] = { 0 };

        for (int j = 0; j < 8; j++) {
            int32_t predicted = tbl0[j] * prev2 + tbl1[j] * prev1;
            for (int k = 0; k < j; k++) {
                predicted += tbl1[(j - k) - 1] * ins[k];
            }
            const int32_t target = (static_cast<int32_t>(want[half * 8 + j]) << 11) - predicted;
            const double step = 2048.0 * static_cast<double>(1 << shift);
            int nibble = static_cast<int>(std::lround(static_cast<double>(target) / step));
            nibble = std::max(-8, std::min(7, nibble));

            ins[j] = static_cast<int16_t>(nibble << shift);
            out[j] = Clamp16((predicted + (static_cast<int32_t>(ins[j]) << 11)) >> 11);

            uint8_t& byte = frame[1 + half * 4 + j / 2];
            if (j % 2 == 0) {
                byte = static_cast<uint8_t>((byte & 0x0F) | ((nibble & 0xF) << 4));
            } else {
                byte = static_cast<uint8_t>((byte & 0xF0) | (nibble & 0xF));
            }
        }
    }

    int64_t error = 0;
    for (int i = 0; i < kFrameSamples; i++) {
        const int64_t diff = static_cast<int64_t>(work[2 + i]) - static_cast<int64_t>(want[i]);
        error += diff * diff;
        got[i] = work[2 + i];
    }
    return error;
}

} // namespace

bool DecodeAudioFile(const std::string& path, int rate, std::vector<int16_t>& out, std::string& error) {
    unsigned int channels = 0;
    unsigned int sourceRate = 0;
    drwav_uint64 frames = 0;
    float* samples = nullptr;

    if (EndsWith(path, ".wav")) {
        samples = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &sourceRate, &frames, nullptr);
    } else if (EndsWith(path, ".mp3")) {
        drmp3_config cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        samples = drmp3_open_file_and_read_pcm_frames_f32(path.c_str(), &cfg, &frames, nullptr);
        channels = cfg.channels;
        sourceRate = cfg.sampleRate;
    } else if (EndsWith(path, ".flac")) {
        samples = drflac_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &sourceRate, &frames, nullptr);
    } else {
        error = "unsupported audio format (use .wav, .mp3 or .flac)";
        return false;
    }

    if (samples == nullptr || frames == 0 || channels == 0 || sourceRate == 0) {
        error = "could not decode " + path;
        return false;
    }

    out = ToMonoAtRate(samples, frames, channels, sourceRate, rate);
    drwav_free(samples, nullptr);
    return !out.empty();
}

VadpcmSample EncodeVadpcm(const std::vector<int16_t>& pcm, int npredictors, int64_t loopStart) {
    VadpcmSample result;
    result.order = kOrder;
    result.npredictors = std::max(1, npredictors);

    double a1 = 0.0, a2 = 0.0;
    FitOrder2(pcm, a1, a2);

    result.book.assign(static_cast<size_t>(result.npredictors) * 16, 0);
    for (int p = 0; p < result.npredictors; p++) {
        const double damp = 1.0 - static_cast<double>(p) / static_cast<double>(result.npredictors * 2);
        BuildBookRows(a1 * damp, a2 * damp, result.book.data() + p * 16, result.book.data() + p * 16 + 8);
    }

    const size_t frameCount = (pcm.size() + kFrameSamples - 1) / kFrameSamples;
    result.frames.assign(frameCount * 9, 0);

    int16_t history[2] = { 0, 0 };
    for (size_t f = 0; f < frameCount; f++) {
        int16_t want[kFrameSamples] = { 0 };
        for (int i = 0; i < kFrameSamples; i++) {
            const size_t at = f * kFrameSamples + i;
            want[i] = at < pcm.size() ? pcm[at] : 0;
        }

        if (loopStart >= 0 && static_cast<size_t>(loopStart) == f * kFrameSamples) {
            result.loopState.assign(16, 0);
            result.loopState[14] = history[0];
            result.loopState[15] = history[1];
        }

        uint8_t best[9] = { 0 };
        int16_t bestGot[kFrameSamples] = { 0 };
        int64_t bestError = INT64_MAX;

        for (int p = 0; p < result.npredictors; p++) {
            const int16_t* tbl0 = result.book.data() + p * 16;
            const int16_t* tbl1 = tbl0 + 8;
            for (int shift = 0; shift <= 12; shift++) {
                uint8_t frame[9] = { 0 };
                int16_t got[kFrameSamples] = { 0 };
                const int64_t err = TryShift(want, history, tbl0, tbl1, shift, p, frame, got);
                if (err < bestError) {
                    bestError = err;
                    std::memcpy(best, frame, 9);
                    std::memcpy(bestGot, got, sizeof(got));
                }
            }
        }

        std::memcpy(result.frames.data() + f * 9, best, 9);
        history[0] = bestGot[kFrameSamples - 2];
        history[1] = bestGot[kFrameSamples - 1];
    }

    if (loopStart >= 0 && result.loopState.empty()) {
        result.loopState.assign(16, 0);
    }

    double signal = 0.0;
    double noise = 0.0;
    {
        int16_t history[2] = { 0, 0 };
        std::vector<int16_t> decoded(kFrameSamples + 2, 0);
        for (size_t f = 0; f < frameCount; f++) {
            const uint8_t* frame = result.frames.data() + f * 9;
            const int predictor = frame[0] & 0xF;
            const int16_t* tbl0 = result.book.data() + predictor * 16;
            decoded[0] = history[0];
            decoded[1] = history[1];
            DecodeFrame(frame, tbl0, tbl0 + 8, decoded.data() + 2);
            for (int i = 0; i < kFrameSamples; i++) {
                const size_t at = f * kFrameSamples + i;
                const double want = at < pcm.size() ? static_cast<double>(pcm[at]) : 0.0;
                const double got = static_cast<double>(decoded[2 + i]);
                signal += want * want;
                noise += (want - got) * (want - got);
            }
            history[0] = decoded[kFrameSamples];
            history[1] = decoded[kFrameSamples + 1];
        }
    }
    result.snrDb = (noise > 0.0 && signal > 0.0) ? 10.0 * std::log10(signal / noise) : 99.0;
    return result;
}

} // namespace BK64
