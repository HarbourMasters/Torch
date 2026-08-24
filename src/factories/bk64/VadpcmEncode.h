#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BK64 {

struct VadpcmSample {
    int32_t order = 2;
    int32_t npredictors = 1;
    std::vector<int16_t> book;
    std::vector<uint8_t> frames;
    std::vector<int16_t> loopState;
    double snrDb = 0.0;
};

bool DecodeAudioFile(const std::string& path, int rate, std::vector<int16_t>& out, std::string& error);
VadpcmSample EncodeVadpcm(const std::vector<int16_t>& pcm, int npredictors, int64_t loopStart);

} // namespace BK64
