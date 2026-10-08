#pragma once

#include "endianness.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace BK64 {

// Reads the fields after a resource's 64-byte header, swapping when the header names the other byte order. Throws
// when it runs out.
class ResourceReader {
  public:
    explicit ResourceReader(const std::vector<uint8_t>& bytes) : mBytes(bytes) {
        if (bytes.size() < 0x40) {
            throw std::runtime_error("the resource is under 64 bytes");
        }
        mSwap = (bytes[0] == (uint8_t)Torch::Endianness::Big) != (Torch::Endianness::Native == Torch::Endianness::Big);
    }

    template <typename T> void Read(T& value) {
        if (mBytes.size() - mAt < sizeof(T)) {
            throw std::runtime_error("the resource ends early");
        }
        for (size_t i = 0; i < sizeof(T); i++) {
            ((uint8_t*)&value)[mSwap ? sizeof(T) - 1 - i : i] = mBytes[mAt + i];
        }
        mAt += sizeof(T);
    }

    template <typename T> T Read() {
        T value;
        Read(value);
        return value;
    }

    template <typename T> void ReadArray(T* values, size_t count) {
        for (size_t i = 0; i < count; i++) {
            Read(values[i]);
        }
    }

    std::vector<uint8_t> Bytes(size_t size) {
        if (mBytes.size() - mAt < size) {
            throw std::runtime_error("the resource ends early");
        }
        mAt += size;
        return std::vector<uint8_t>(mBytes.begin() + (mAt - size), mBytes.begin() + mAt);
    }

  private:
    const std::vector<uint8_t>& mBytes;
    size_t mAt = 0x40;
    bool mSwap = false;
};

} // namespace BK64
