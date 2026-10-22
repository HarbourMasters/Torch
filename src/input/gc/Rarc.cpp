#include "Rarc.h"
#include "BlockReader.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

extern "C" {
#include <libyaz0/yaz0.h>
#include <libyay0/yay0.h>
}

namespace GC {

std::vector<uint8_t> Rarc::Decode(const std::vector<uint8_t>& data) {
    if (data.size() < 16) {
        return data;
    }
    uint32_t size = 0;
    uint8_t* decoded = nullptr;
    if (std::memcmp(data.data(), "Yaz0", 4) == 0) {
        decoded = yaz0_decode(data.data(), &size);
    } else if (std::memcmp(data.data(), "Yay0", 4) == 0) {
        decoded = yay0_decode(data.data(), &size);
    } else {
        return data;
    }
    if (decoded == nullptr) {
        throw std::runtime_error("RARC: failed to decode compressed data");
    }
    std::vector<uint8_t> out(decoded, decoded + size);
    free(decoded);
    return out;
}

Rarc::Rarc(const std::vector<uint8_t>& raw) {
    const auto arc = Decode(raw);
    if (arc.size() < 0x40 || std::memcmp(arc.data(), "RARC", 4) != 0) {
        throw std::runtime_error("Not a RARC archive");
    }
    const uint8_t* h = arc.data();
    mDataStart = 0x20 + ReadBE32(h + 0x0C);
    mNumNodes = ReadBE32(h + 0x20);
    mNodeTable = 0x20 + ReadBE32(h + 0x24);
    mNumEntries = ReadBE32(h + 0x28);
    mEntryTable = 0x20 + ReadBE32(h + 0x2C);
    mStringTableSize = ReadBE32(h + 0x30);
    mStringTable = 0x20 + ReadBE32(h + 0x34);

    if (mNumNodes == 0 || static_cast<uint64_t>(mNodeTable) + mNumNodes * 0x10ULL > arc.size() ||
        static_cast<uint64_t>(mEntryTable) + mNumEntries * 0x14ULL > arc.size() ||
        static_cast<uint64_t>(mStringTable) + mStringTableSize > arc.size()) {
        throw std::runtime_error("RARC: tables out of range");
    }

    WalkNode(arc, 0, "", 0);
}

void Rarc::WalkNode(const std::vector<uint8_t>& arc, uint32_t node, const std::string& prefix, int depth) {
    if (node >= mNumNodes || depth > 64) {
        throw std::runtime_error("RARC: invalid directory structure");
    }

    auto nameAt = [&](uint32_t offset) {
        if (offset >= mStringTableSize) {
            throw std::runtime_error("RARC: name out of range");
        }
        const char* s = reinterpret_cast<const char*>(arc.data() + mStringTable + offset);
        return std::string(s, strnlen(s, mStringTableSize - offset));
    };

    const uint8_t* n = arc.data() + mNodeTable + node * 0x10;
    if (node == 0) {
        mRootName = nameAt(ReadBE32(n + 4));
    }
    const uint16_t count = ReadBE16(n + 0x0A);
    const uint32_t first = ReadBE32(n + 0x0C);
    if (static_cast<uint64_t>(first) + count > mNumEntries) {
        throw std::runtime_error("RARC: node entries out of range");
    }

    for (uint32_t i = first; i < first + count; i++) {
        const uint8_t* e = arc.data() + mEntryTable + i * 0x14;
        const uint16_t id = ReadBE16(e);
        const uint32_t typeName = ReadBE32(e + 4);
        const uint8_t flags = typeName >> 24;
        const std::string name = nameAt(typeName & 0xFFFFFF);
        const uint32_t offset = ReadBE32(e + 8);
        const uint32_t size = ReadBE32(e + 0x0C);

        if (flags & FLAG_DIR) {
            if (name == "." || name == "..") {
                continue;
            }
            WalkNode(arc, offset, prefix + name + "/", depth + 1);
            continue;
        }

        const uint64_t start = static_cast<uint64_t>(mDataStart) + offset;
        if (start + size > arc.size()) {
            throw std::runtime_error("RARC: member " + prefix + name + " out of range");
        }
        RarcMember member = { prefix + name, id, flags,
                              std::vector<uint8_t>(arc.begin() + start, arc.begin() + start + size) };
        if (flags & FLAG_COMPRESSED) {
            member.data = Decode(member.data);
        }
        mMembers.push_back(std::move(member));
    }
}

} // namespace GC
