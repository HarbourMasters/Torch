#include "SoundfontTblFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace BK64 {

namespace {

uint16_t ReadU16BE(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
int16_t ReadS16BE(const uint8_t* p) {
    return (int16_t)ReadU16BE(p);
}
uint32_t ReadU32BE(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
int32_t ReadS32BE(const uint8_t* p) {
    return (int32_t)ReadU32BE(p);
}

// Walk an N64 ALBankFile binary (big-endian, 32-bit offsets relative to ctl
// start) and return max(wavetable.base + wavetable.len) over every wavetable
// reachable from the file's banks. Throws on any structural inconsistency —
// silent fallbacks would just produce a wrong-size tbl and reproduce the bug
// we're fixing.
//
//   ALBankFile:  [0] s16 revision, [2] s16 bankCount, [4+] u32 bankOffsets[]
//   ALBank:      [0] s16 instCount, [4] s32 sampleRate, [8] u32 percussion,
//                [12+] u32 instOffsets[]
//   ALInstrument:[14] s16 soundCount, [16+] u32 soundOffsets[]
//   ALSound:     [8] u32 wavetable
//   ALWaveTable: [0] u32 base, [4] s32 len
size_t ComputeTblSize(const uint8_t* ctl, size_t ctlSize, uint32_t ctlRomOffset) {
    auto check = [&](uint32_t off, size_t need, const char* what) {
        if ((size_t)off + need > ctlSize) {
            SPDLOG_ERROR("SoundfontTblFactory: ctl walk OOB reading {} (ctl@0x{:X} size=0x{:X} off=0x{:X} need=0x{:X})",
                         what, ctlRomOffset, ctlSize, off, need);
            throw std::runtime_error("SoundfontTblFactory: ctl walk OOB");
        }
    };

    check(0, 4, "header");
    int16_t bankCount = ReadS16BE(ctl + 2);
    if (bankCount <= 0) {
        throw std::runtime_error("SoundfontTblFactory: bankCount <= 0");
    }

    uint64_t maxEnd = 0;
    std::unordered_set<uint32_t> seenWt;

    for (int b = 0; b < bankCount; b++) {
        check(4 + b * 4, 4, "bankArray entry");
        uint32_t bankOff = ReadU32BE(ctl + 4 + b * 4);
        if (bankOff == 0) {
            continue;
        }

        check(bankOff, 12, "ALBank header");
        int16_t instCount = ReadS16BE(ctl + bankOff + 0);
        uint32_t percOff = ReadU32BE(ctl + bankOff + 8);

        auto visitInst = [&](uint32_t instOff) {
            if (instOff == 0) {
                return;
            }
            check(instOff, 16, "ALInstrument header");
            int16_t soundCount = ReadS16BE(ctl + instOff + 14);
            for (int s = 0; s < soundCount; s++) {
                check(instOff + 16 + s * 4, 4, "ALInstrument soundArray entry");
                uint32_t sndOff = ReadU32BE(ctl + instOff + 16 + s * 4);
                if (sndOff == 0) {
                    continue;
                }
                check(sndOff, 12, "ALSound header");
                uint32_t wtOff = ReadU32BE(ctl + sndOff + 8);
                if (wtOff == 0 || !seenWt.insert(wtOff).second) {
                    continue;
                }
                check(wtOff, 8, "ALWaveTable header");
                uint32_t base = ReadU32BE(ctl + wtOff + 0);
                int32_t len = ReadS32BE(ctl + wtOff + 4);
                if (len < 0) {
                    throw std::runtime_error("SoundfontTblFactory: negative wavetable len");
                }
                uint64_t end = (uint64_t)base + (uint64_t)len;
                if (end > maxEnd) {
                    maxEnd = end;
                }
            }
        };

        visitInst(percOff);
        for (int i = 0; i < instCount; i++) {
            check(bankOff + 12 + i * 4, 4, "ALBank instArray entry");
            visitInst(ReadU32BE(ctl + bankOff + 12 + i * 4));
        }
    }

    if (maxEnd == 0) {
        throw std::runtime_error("SoundfontTblFactory: ctl referenced no wavetables");
    }

    // Round up to BK's 16-byte ROM alignment.
    return (size_t)((maxEnd + 0xF) & ~(uint64_t)0xF);
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> SoundfontTblFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto tblOffset = GetSafeNode<uint32_t>(node, "offset");
    const auto ctlOffset = GetSafeNode<uint32_t>(node, "ctl_offset");
    const auto ctlSize = GetSafeNode<uint32_t>(node, "ctl_size");

    if ((size_t)ctlOffset + ctlSize > buffer.size()) {
        throw std::runtime_error("SoundfontTblFactory: ctl_offset + ctl_size exceeds ROM size");
    }

    const size_t tblSize = ComputeTblSize(buffer.data() + ctlOffset, ctlSize, ctlOffset);

    if ((size_t)tblOffset + tblSize > buffer.size()) {
        throw std::runtime_error("SoundfontTblFactory: computed tbl size exceeds ROM bounds");
    }

    SPDLOG_INFO("SoundfontTbl: tbl@0x{:X} size 0x{:X} (computed from ctl@0x{:X})", tblOffset, tblSize, ctlOffset);

    return std::make_shared<RawBuffer>(buffer.data() + tblOffset, tblSize);
}

} // namespace BK64
