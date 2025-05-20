#include "TorchUtils.h"

#include <stack>
#include <Companion.h>
#include <factories/BaseFactory.h>
#include "spdlog/spdlog.h"

namespace fs = std::filesystem;

uint32_t Torch::translate(const uint32_t offset) {
    if(SEGMENT_NUMBER(offset) > 0x01) {
        auto segment = SEGMENT_NUMBER(offset);
        const auto addr = Companion::Instance->GetFileOffsetFromSegmentedAddr(segment);
        if(!addr.has_value()) {
            SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", segment);
            throw std::runtime_error("Failed to find offset");
        }

        return addr.value() + SEGMENT_OFFSET(offset);
    }

    return offset;
}

int getFileDepth(const fs::path& base, const fs::path& p) {
    return std::distance(base.begin(), p.begin());
}

std::vector<fs::directory_entry> Torch::getRecursiveEntries(const fs::path baseDir) {
    std::vector<fs::directory_entry> result;

    for (const auto& entry : fs::recursive_directory_iterator(baseDir)) {
        result.push_back(entry);
    }

    std::sort(result.begin(), result.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        std::string nameA = a.path().parent_path().string();
        std::string nameB = b.path().parent_path().string();
        int depthA = std::distance(a.path().begin(), a.path().end());
        int depthB = std::distance(b.path().begin(), b.path().end());

        if (nameA != nameB)
            return nameA < nameB;
        return depthA < depthB;
    });

    return result;
}