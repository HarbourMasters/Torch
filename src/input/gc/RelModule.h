#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace GC {

enum RelocationType : uint8_t {
    R_PPC_NONE = 0,
    R_PPC_ADDR32 = 1,
    R_PPC_ADDR24 = 2,
    R_PPC_ADDR16 = 3,
    R_PPC_ADDR16_LO = 4,
    R_PPC_ADDR16_HI = 5,
    R_PPC_ADDR16_HA = 6,
    R_PPC_ADDR14 = 7,
    R_PPC_ADDR14_BRTAKEN = 8,
    R_PPC_ADDR14_BRNTAKEN = 9,
    R_PPC_REL24 = 10,
    R_PPC_REL14 = 11,
    R_PPC_REL14_BRTAKEN = 12,
    R_PPC_REL14_BRNTAKEN = 13,
    R_DOLPHIN_NOP = 201,
    R_DOLPHIN_SECTION = 202,
    R_DOLPHIN_END = 203,
};

struct RelSectionInfo {
    uint32_t offset;
    uint32_t size;
    bool exec;

    bool IsBss() const { return offset == 0 && size != 0; }
};

struct RelImport {
    uint32_t moduleId;
    uint32_t offset;
};

struct RelRelocation {
    uint16_t offset;
    uint8_t type;
    uint8_t section;
    uint32_t addend;
};

struct RelUnresolved {
    uint32_t section;
    uint32_t offset;
    uint8_t type;
    uint8_t targetSection;
    uint32_t addend;
};

struct RelLinkStats {
    uint32_t applied = 0;
    uint32_t skippedUnplaced = 0;
    uint32_t unresolvedTarget = 0;
    uint32_t unknownModule = 0;
    uint32_t unknownType = 0;
    std::map<uint8_t, uint32_t> byType;
    std::vector<RelUnresolved> unresolved;
};

class RelModule {
public:
    explicit RelModule(std::vector<uint8_t> data);

    static std::vector<uint8_t> Decompress(const std::vector<uint8_t>& data);

    uint32_t GetId() const { return mId; }
    uint32_t GetVersion() const { return mVersion; }
    uint32_t GetBssSize() const { return mBssSize; }
    const std::vector<RelSectionInfo>& GetSections() const { return mSections; }
    const std::vector<RelImport>& GetImports() const { return mImports; }
    std::vector<RelRelocation> GetRelocations(const RelImport& imp) const;

    std::vector<std::string> GetSectionNames() const;
    std::optional<uint32_t> FindSection(const std::string& name) const;

    std::vector<std::vector<uint8_t>> GetSectionData() const;

    RelLinkStats Link(const std::vector<std::optional<uint32_t>>& bases,
                      std::vector<std::vector<uint8_t>>& sections) const;

private:
    std::vector<uint8_t> mData;
    uint32_t mId;
    uint32_t mVersion;
    uint32_t mBssSize;
    uint32_t mRelOffset;
    uint32_t mImpOffset;
    uint32_t mImpSize;
    std::vector<RelSectionInfo> mSections;
    std::vector<RelImport> mImports;
};

} // namespace GC
