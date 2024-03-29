#pragma once

#include <BinaryReader.h>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <yaml-cpp/yaml.h>
#include <optional>
#include <cstdint>

enum class CompressionType {
    None,
    MIO0,
    YAY0,
    YAZ0,
};


struct DataChunk {
    uint8_t* data;
    size_t size;
};

struct DecompressedData {
    DataChunk* root;
    DataChunk segment;

    LUS::BinaryReader GetReader() const {
        return LUS::BinaryReader(segment.data, segment.size);
    }
};

class Decompressor {
public:
    static DataChunk* Decode(const std::vector<uint8_t>& buffer, uint32_t offset, CompressionType type);
    static DecompressedData AutoDecode(YAML::Node& node, std::vector<uint8_t>& buffer, std::optional<size_t> size = std::nullopt);
    static DecompressedData AutoDecodeByOffset(uint32_t offset, size_t size, std::vector<uint8_t>& buffer);
    static uint32_t TranslateAddr(uint32_t addr, bool baseAddress = false);
    static bool IsSegmented(uint32_t addr);

    static void ClearCache();
};