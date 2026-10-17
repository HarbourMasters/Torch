#pragma once

#include "input/gc/RelModule.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Synthetic {

struct RelSpec {
    struct Section {
        bool exec;
        std::vector<uint8_t> data;
        uint32_t bssSize;
    };
    uint32_t id;
    std::vector<Section> sections;
    std::vector<std::pair<uint32_t, std::vector<GC::RelRelocation>>> imports;
};

std::vector<uint8_t> BuildRel(const RelSpec& spec);
GC::RelRelocation Rel(uint16_t offset, uint8_t type, uint8_t section, uint32_t addend);

struct ArcEntry {
    std::string name;
    uint8_t flags;
    uint32_t node;
    std::vector<uint8_t> data;
};

struct ArcNode {
    std::string type;
    std::string name;
    std::vector<ArcEntry> entries;
};

std::vector<uint8_t> BuildArc(const std::vector<ArcNode>& nodes);
std::vector<uint8_t> Yay0(const std::vector<uint8_t>& data);

struct DolSectionSpec {
    uint32_t address;
    std::vector<uint8_t> data;
};

std::vector<uint8_t> BuildDol(const std::vector<DolSectionSpec>& text, const std::vector<DolSectionSpec>& data,
                              uint32_t bssAddress, uint32_t bssSize);

std::vector<uint8_t> BuildGcm(const std::string& gameId, uint8_t revision, const std::vector<uint8_t>& dol,
                              const std::map<std::string, std::vector<uint8_t>>& files);

std::vector<uint8_t> BuildCiso(const std::vector<uint8_t>& image, uint32_t blockSize);

} // namespace Synthetic
