#include "AIFCWriter.h"

#include "hj/pyutils.h"

#define ALIGN(val, al) (size_t) ((val + (al - 1)) & -al)

std::vector<char> AIFCWriter::pack(const std::string& str) {
    std::vector<char> result;
    result.insert(result.end(), str.begin(), str.end());
    return result;
}

std::vector<char> AIFCWriter::pstring(const std::vector<char>& data) {
    std::vector<char> result;
    result.insert(result.end(), data.begin(), data.end());
    if (data.size() % 2 == 0) {
        result.push_back('\0');
    }
    return result;
}

void AIFCWriter::add_section(AIFC::MagicValues magic, const std::vector<char>& data) {
    sections.emplace_back(magic, data);
    total_size += ALIGN(data.size(), 2) + 8;
}

void AIFCWriter::add_custom_section(const std::string& section, const std::vector<char>& data) {
    this->add_section(AIFC::MagicValues::AAPL, pack("stoc") + pstring(pack(section)) + data);
}

void AIFCWriter::finish() {
    // total_size isn't used and is regularly wrong.
    // In particular, vadpcm_enc preserves the size of the input file...
    total_size += 4;

    out.Write(BSWAP32(AIFC::MagicValues::FORM));
    out.Write((uint32_t) BSWAP32(total_size));
    out.Write(BSWAP32(AIFC::MagicValues::AIFC));

    for (const auto& section : sections) {
        out.Write((uint32_t) BSWAP32(section.first));
        out.Write((uint32_t) BSWAP32(section.second.size()));
        out.Write((char*) section.second.data(), section.second.size());
        if(section.second.size() % 2) {
            out.Write('\0');
        }
    }
}
