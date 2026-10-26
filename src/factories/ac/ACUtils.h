#pragma once

#include "factories/BaseFactory.h"

#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace AC {

std::string Hex(uint32_t v);
std::string Upper(std::string s);
std::string TypeOf(YAML::Node node);
std::string NameOf(YAML::Node& node);

std::vector<uint8_t> ReadBytes(uint32_t addr, uint32_t size, const std::string& what);
std::string AssetPath(uint32_t addr, const std::string& type, const std::string& what);

std::optional<uint32_t> CountHint(YAML::Node& node, const std::string& key);
void CheckCount(YAML::Node& node, const std::string& key, uint32_t computed, const std::string& name);

void WriteBytes(LUS::BinaryWriter& writer, const std::vector<uint8_t>& bytes);

inline uint16_t Be16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

inline uint32_t Be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

class Cursor {
public:
    Cursor(std::vector<uint8_t> data, const std::string& what) : mData(std::move(data)), mWhat(what) {}

    uint8_t U8() { return Take(1)[0]; }
    int8_t S8() { return (int8_t)U8(); }
    uint16_t U16() { return Be16(Take(2)); }
    int16_t S16() { return (int16_t)U16(); }
    uint32_t U32() { return Be32(Take(4)); }
    int32_t S32() { return (int32_t)U32(); }
    float F32() {
        const uint32_t v = U32();
        float f;
        std::memcpy(&f, &v, sizeof(f));
        return f;
    }
    void Skip(size_t n) { Take(n); }

private:
    const uint8_t* Take(size_t n) {
        if (mPos + n > mData.size()) {
            throw std::runtime_error(mWhat + ": read past the end of the structure");
        }
        const uint8_t* p = mData.data() + mPos;
        mPos += n;
        return p;
    }

    std::vector<uint8_t> mData;
    std::string mWhat;
    size_t mPos = 0;
};

} // namespace AC
