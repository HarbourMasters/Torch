#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace BK64 {

class SlotSizer {
  public:
    SlotSizer(const std::vector<uint32_t>& offsets, uint32_t dataStart, size_t romSize)
        : mStray(offsets.size(), false), mDataStart(dataStart), mRomSize(romSize) {
        mBoundaries.reserve(offsets.size());
        uint32_t high = 0;
        for (size_t i = 0; i < offsets.size(); i++) {
            if (offsets[i] < high) {
                mStray[i] = true;
                mStrayCount++;
                continue;
            }
            high = offsets[i];
            mBoundaries.push_back(offsets[i]);
        }
        std::sort(mBoundaries.begin(), mBoundaries.end());
        mBoundaries.erase(std::unique(mBoundaries.begin(), mBoundaries.end()), mBoundaries.end());
        mRegionEnd = mBoundaries.empty() ? 0 : mBoundaries.back();
    }

    uint32_t operator()(uint32_t off) const {
        if (off >= mRegionEnd) {
            return 0;
        }
        const auto next = std::upper_bound(mBoundaries.begin(), mBoundaries.end(), off);
        const uint32_t end = std::min(next != mBoundaries.end() ? *next : mRegionEnd, mRegionEnd);
        if (end <= off) {
            return 0;
        }
        const uint64_t start = static_cast<uint64_t>(mDataStart) + off;
        if (start >= mRomSize) {
            return 0;
        }
        return static_cast<uint32_t>(std::min<uint64_t>(end - off, mRomSize - start));
    }

    bool IsStray(size_t index) const {
        return index < mStray.size() && mStray[index];
    }

    size_t StrayCount() const {
        return mStrayCount;
    }

    uint32_t RegionEnd() const {
        return mRegionEnd;
    }

  private:
    std::vector<uint32_t> mBoundaries;
    std::vector<bool> mStray;
    size_t mStrayCount = 0;
    uint32_t mRegionEnd = 0;
    uint32_t mDataStart;
    size_t mRomSize;
};

} // namespace BK64
