#include "RelModule.h"
#include "BlockReader.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

extern "C" {
#include <libyaz0/yaz0.h>
}

namespace GC {

static constexpr uint32_t REL_HEADER_V1_SIZE = 0x40;

RelModule::RelModule(std::vector<uint8_t> data) : mData(std::move(data)) {
    if (mData.size() < REL_HEADER_V1_SIZE) {
        throw std::runtime_error("REL: file too small");
    }
    const uint8_t* h = mData.data();
    mId = ReadBE32(h + 0x00);
    const uint32_t numSections = ReadBE32(h + 0x0C);
    const uint32_t sectionInfoOffset = ReadBE32(h + 0x10);
    mVersion = ReadBE32(h + 0x1C);
    mBssSize = ReadBE32(h + 0x20);
    mRelOffset = ReadBE32(h + 0x24);
    mImpOffset = ReadBE32(h + 0x28);
    mImpSize = ReadBE32(h + 0x2C);

    if (static_cast<uint64_t>(sectionInfoOffset) + numSections * 8ULL > mData.size()) {
        throw std::runtime_error("REL: section table out of range");
    }
    for (uint32_t i = 0; i < numSections; i++) {
        const uint8_t* s = h + sectionInfoOffset + i * 8;
        const uint32_t raw = ReadBE32(s);
        RelSectionInfo info = { raw & ~1u, ReadBE32(s + 4), (raw & 1) != 0 };
        if (info.offset != 0 && static_cast<uint64_t>(info.offset) + info.size > mData.size()) {
            throw std::runtime_error("REL: section " + std::to_string(i) + " out of range");
        }
        mSections.push_back(info);
    }

    if (static_cast<uint64_t>(mImpOffset) + mImpSize > mData.size()) {
        throw std::runtime_error("REL: import table out of range");
    }
    for (uint32_t off = 0; off + 8 <= mImpSize; off += 8) {
        mImports.push_back({ ReadBE32(h + mImpOffset + off), ReadBE32(h + mImpOffset + off + 4) });
    }
}

std::vector<uint8_t> RelModule::Decompress(const std::vector<uint8_t>& data) {
    if (data.size() < 16 || std::memcmp(data.data(), "Yaz0", 4) != 0) {
        return data;
    }
    uint32_t size = 0;
    uint8_t* decoded = yaz0_decode(data.data(), &size);
    if (decoded == nullptr) {
        throw std::runtime_error("Failed to decode Yaz0");
    }
    std::vector<uint8_t> out(decoded, decoded + size);
    free(decoded);
    return out;
}

std::vector<RelRelocation> RelModule::GetRelocations(const RelImport& imp) const {
    std::vector<RelRelocation> out;
    for (uint64_t off = imp.offset; off + 8 <= mData.size(); off += 8) {
        const uint8_t* r = mData.data() + off;
        RelRelocation rel = { ReadBE16(r), r[2], r[3], ReadBE32(r + 4) };
        out.push_back(rel);
        if (rel.type == R_DOLPHIN_END) {
            return out;
        }
    }
    throw std::runtime_error("REL: relocation list without R_DOLPHIN_END");
}

std::vector<std::string> RelModule::GetSectionNames() const {
    static const char* standard[] = { "", ".text", ".ctors", ".dtors", ".rodata", ".data", ".bss" };
    std::vector<std::string> names(mSections.size());

    const bool isStandard = mSections.size() == 7 && mSections[1].exec && !mSections[6].exec &&
                            (mSections[6].IsBss() || mSections[6].size == 0);
    if (isStandard) {
        for (size_t i = 0; i < names.size(); i++) {
            names[i] = standard[i];
        }
        return names;
    }

    std::vector<size_t> data;
    int texts = 0;
    for (size_t i = 0; i < mSections.size(); i++) {
        const auto& s = mSections[i];
        if (s.offset == 0 && s.size == 0) {
            continue;
        }
        if (s.IsBss()) {
            names[i] = ".bss";
        } else if (s.exec) {
            names[i] = texts++ == 0 ? ".text" : ".text" + std::to_string(texts - 1);
        } else {
            data.push_back(i);
        }
    }
    static const char* dataNames[] = { ".ctors", ".dtors", ".rodata", ".data" };
    for (size_t j = 0; j < data.size(); j++) {
        names[data[j]] = data.size() == 4 ? dataNames[j] : ".data" + std::to_string(j);
    }
    return names;
}

std::optional<uint32_t> RelModule::FindSection(const std::string& name) const {
    const auto names = GetSectionNames();
    for (size_t i = 0; i < names.size(); i++) {
        if (names[i] == name) {
            return static_cast<uint32_t>(i);
        }
    }
    char* end = nullptr;
    const unsigned long index = std::strtoul(name.c_str(), &end, 0);
    if (!name.empty() && end != nullptr && *end == '\0' && index < mSections.size()) {
        return static_cast<uint32_t>(index);
    }
    return std::nullopt;
}

std::vector<std::vector<uint8_t>> RelModule::GetSectionData() const {
    std::vector<std::vector<uint8_t>> out;
    for (const auto& s : mSections) {
        if (s.offset == 0) {
            out.emplace_back(s.size, 0);
        } else {
            out.emplace_back(mData.begin() + s.offset, mData.begin() + s.offset + s.size);
        }
    }
    return out;
}

RelLinkStats RelModule::Link(const std::vector<std::optional<uint32_t>>& bases,
                             std::vector<std::vector<uint8_t>>& sections) const {
    RelLinkStats stats;

    for (const auto& imp : this->mImports) {
        const bool self = imp.moduleId == this->mId;
        if (!self && imp.moduleId != 0) {
            for (const auto& rel : GetRelocations(imp)) {
                if (rel.type < R_DOLPHIN_NOP) {
                    stats.unknownModule++;
                }
            }
            continue;
        }

        std::optional<uint32_t> section;
        uint32_t cursor = 0;

        for (const auto& rel : GetRelocations(imp)) {
            cursor += rel.offset;

            if (rel.type == R_DOLPHIN_END) {
                break;
            }
            if (rel.type == R_DOLPHIN_NOP) {
                continue;
            }
            if (rel.type == R_DOLPHIN_SECTION) {
                section = rel.section;
                cursor = 0;
                continue;
            }
            if (rel.type == R_PPC_NONE) {
                continue;
            }

            if (!section.has_value() || section.value() >= sections.size() || section.value() >= bases.size() ||
                !bases[section.value()].has_value()) {
                stats.skippedUnplaced++;
                continue;
            }

            uint32_t offset = 0;
            if (self) {
                if (rel.section >= bases.size() || !bases[rel.section].has_value()) {
                    stats.unresolvedTarget++;
                    stats.unresolved.push_back({ section.value(), cursor, rel.type, rel.section, rel.addend });
                    continue;
                }
                offset = bases[rel.section].value();
            }

            auto& data = sections[section.value()];
            const uint32_t width = rel.type == R_PPC_ADDR32 || rel.type == R_PPC_ADDR24 ||
                                           rel.type == R_PPC_REL24 || rel.type == R_PPC_ADDR14 ||
                                           rel.type == R_PPC_ADDR14_BRTAKEN || rel.type == R_PPC_ADDR14_BRNTAKEN ||
                                           rel.type == R_PPC_REL14 || rel.type == R_PPC_REL14_BRTAKEN ||
                                           rel.type == R_PPC_REL14_BRNTAKEN
                                       ? 4
                                       : 2;
            if (static_cast<uint64_t>(cursor) + width > data.size()) {
                throw std::runtime_error("REL: relocation writes past the end of section " +
                                         std::to_string(section.value()));
            }

            uint8_t* p = data.data() + cursor;
            const uint32_t where = bases[section.value()].value() + cursor;
            const uint32_t x = offset + rel.addend;

            switch (rel.type) {
                case R_PPC_ADDR32:
                    WriteBE32(p, x);
                    break;
                case R_PPC_ADDR24:
                    WriteBE32(p, (ReadBE32(p) & ~0x03FFFFFCu) | (x & 0x03FFFFFCu));
                    break;
                case R_PPC_ADDR16:
                case R_PPC_ADDR16_LO:
                    WriteBE16(p, x & 0xFFFF);
                    break;
                case R_PPC_ADDR16_HI:
                    WriteBE16(p, (x >> 16) & 0xFFFF);
                    break;
                case R_PPC_ADDR16_HA:
                    WriteBE16(p, ((x >> 16) + ((x & 0x8000) ? 1 : 0)) & 0xFFFF);
                    break;
                case R_PPC_REL24:
                    WriteBE32(p, (ReadBE32(p) & ~0x03FFFFFCu) | ((x - where) & 0x03FFFFFCu));
                    break;
                case R_PPC_ADDR14:
                case R_PPC_ADDR14_BRTAKEN:
                case R_PPC_ADDR14_BRNTAKEN:
                    WriteBE32(p, (ReadBE32(p) & ~0xFFFCu) | (x & 0xFFFCu));
                    break;
                case R_PPC_REL14:
                case R_PPC_REL14_BRTAKEN:
                case R_PPC_REL14_BRNTAKEN:
                    WriteBE32(p, (ReadBE32(p) & ~0xFFFCu) | ((x - where) & 0xFFFCu));
                    break;
                default:
                    stats.unknownType++;
                    continue;
            }
            stats.applied++;
            stats.byType[rel.type]++;
        }
    }

    return stats;
}

} // namespace GC
