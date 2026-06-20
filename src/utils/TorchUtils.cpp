#include "TorchUtils.h"

#include <stack>
#include <Companion.h>
#include <factories/BaseFactory.h>
#include "spdlog/spdlog.h"

namespace fs = std::filesystem;

uint32_t Torch::translate(const uint32_t offset) {
    // Segment 0 is a raw offset. BK64 also translates segment 1 (it stores assets
    // in compressed segments); other games leave segment 1 raw.
    const uint32_t firstSegment =
        Companion::Instance->GetGBIMinorVersion() == GBIMinorVersion::BK64 ? 0x00 : 0x01;
    if (SEGMENT_NUMBER(offset) > firstSegment) {
        auto segment = SEGMENT_NUMBER(offset);
        const auto addr = Companion::Instance->GetFileOffsetFromSegmentedAddr(segment);
        if (!addr.has_value()) {
            // Fall back to the compressed-segment map
            const auto compressedSegmentPair = Companion::Instance->GetFileOffsetFromCompressedSegmentedAddr(segment);

            if (!compressedSegmentPair.has_value()) {
                SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", segment);
                throw std::runtime_error("Failed to find offset");
            }
            return compressedSegmentPair.value().first + compressedSegmentPair.value().second + SEGMENT_OFFSET(offset);
        }

        return addr.value() + SEGMENT_OFFSET(offset);
    }

    return offset;
}

int getFileDepth(const fs::path& base, const fs::path& p) {
    return std::distance(base.begin(), p.begin());
}

std::vector<fs::directory_entry> Torch::getRecursiveEntries(const fs::path baseDir) {
    std::set<fs::directory_entry> result;

    for (const auto& entry : fs::recursive_directory_iterator(baseDir)) {
        result.insert(entry);
    }

    std::vector<fs::directory_entry> sortedEntries(result.begin(), result.end());
    return sortedEntries;
}