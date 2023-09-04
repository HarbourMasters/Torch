#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include "binarytools/BinaryWriter.h"

namespace AIFC {
    enum MagicValues {
        FORM = 0x464f524d,
        AIFC = 0x41494643,
        COMM = 0x434f4d4d,
        INST = 0x494e5354,
        VAPC = 0x56415043,
        SSND = 0x53534e44,
        AAPL = 0x4150504c,
        stoc = 0x73746f63,
    };
}

class AIFCWriter {
public:
    AIFCWriter(LUS::BinaryWriter& out) : out(out), total_size(0) {}

    static std::vector<char> pack(const std::string& str);
    static std::vector<char> pstring(const std::vector<char>& data);
    void add_section(AIFC::MagicValues, const std::vector<char>& data);
    void add_custom_section(const std::string& section, const std::vector<char>& data);
    void finish();

private:
    LUS::BinaryWriter& out;
    std::vector<std::pair<AIFC::MagicValues, std::vector<char>>> sections;
    std::size_t total_size;
};