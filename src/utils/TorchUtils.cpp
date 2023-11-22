#include "TorchUtils.h"

#include <Companion.h>
#include <factories/BaseFactory.h>
#include "spdlog/spdlog.h"

uint32_t Torch::translate(const uint32_t offset) {
    if(SEGMENT_NUMBER(offset) > 0x01) {
        auto segment = SEGMENT_NUMBER(offset);
        const auto addr = Companion::Instance->GetSegmentedAddr(segment);
        if(!addr.has_value()) {
            SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", segment);
            throw std::runtime_error("Failed to find offset");
        }

        return addr.value() + SEGMENT_OFFSET(offset);
    }

    return offset;
}
