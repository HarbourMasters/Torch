#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace GC {

struct RarcMember {
    std::string path;
    uint16_t id;
    uint8_t flags;
    std::vector<uint8_t> data;
};

class Rarc {
public:
    static constexpr uint8_t FLAG_FILE = 0x01;
    static constexpr uint8_t FLAG_DIR = 0x02;
    static constexpr uint8_t FLAG_COMPRESSED = 0x04;
    static constexpr uint8_t FLAG_YAZ0 = 0x80;

    explicit Rarc(const std::vector<uint8_t>& data);

    const std::vector<RarcMember>& GetMembers() const { return mMembers; }
    const std::string& GetRootName() const { return mRootName; }

    static std::vector<uint8_t> Decode(const std::vector<uint8_t>& data);

private:
    void WalkNode(const std::vector<uint8_t>& arc, uint32_t node, const std::string& prefix, int depth);

    std::string mRootName;
    std::vector<RarcMember> mMembers;
    uint32_t mNodeTable;
    uint32_t mNumNodes;
    uint32_t mEntryTable;
    uint32_t mNumEntries;
    uint32_t mStringTable;
    uint32_t mStringTableSize;
    uint32_t mDataStart;
};

} // namespace GC
