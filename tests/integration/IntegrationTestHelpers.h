#pragma once

#include <gtest/gtest.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <cstring>

namespace fs = std::filesystem;

// ROM SHA-1 constants
constexpr const char* SM64_US_SHA1 = "9bef1128717f958171a4afac3ed78ee2bb4e86ce";

inline std::string GetRomDir() {
    return std::string(INTEGRATION_ROM_DIR);
}

inline std::string GetTestDir() {
    return std::string(INTEGRATION_TEST_DIR);
}

inline bool RomExists(const std::string& filename) {
    return fs::exists(fs::path(GetRomDir()) / filename);
}

// Binary header parsed from an exported asset
struct AssetHeader {
    int8_t endianness;
    uint32_t resourceType;
    uint32_t version;
    uint64_t deadbeef;
};

inline AssetHeader ParseHeader(const std::vector<char>& data) {
    AssetHeader header{};
    if (data.size() < 0x40) return header;

    std::memcpy(&header.endianness, data.data() + 0x00, 1);
    std::memcpy(&header.resourceType, data.data() + 0x04, 4);
    std::memcpy(&header.version, data.data() + 0x08, 4);
    std::memcpy(&header.deadbeef, data.data() + 0x0C, 8);
    return header;
}

inline void ValidateHeader(const std::vector<char>& data, uint32_t expectedResourceType) {
    ASSERT_GE(data.size(), 0x40u) << "Asset too small to contain header";
    auto header = ParseHeader(data);
    EXPECT_EQ(header.endianness, 0) << "Expected Native endianness";
    EXPECT_EQ(header.resourceType, expectedResourceType) << "Resource type mismatch";
    EXPECT_EQ(header.version, 0u) << "Expected version 0";
    EXPECT_EQ(header.deadbeef, 0xDEADBEEFDEADBEEFULL) << "Missing DEADBEEF marker";
}

// Run the full Companion pipeline for a given config directory and ROM path.
// Returns a map of asset name -> binary content extracted from the output .o2r.
// Defined in IntegrationTestHelpers.cpp to avoid miniz ODR violations.
std::map<std::string, std::vector<char>> RunPipeline(const std::string& configDir, const std::string& romPath);
