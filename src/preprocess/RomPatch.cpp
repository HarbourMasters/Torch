#include "RomPatch.h"

#include <cstring>
#include <map>
#include <stdexcept>

namespace RomPatch {

namespace {

constexpr uint32_t kExtInStart = 0x000D0000;
constexpr uint32_t kExtOutStart = 0x00800000;
constexpr uint32_t kExtSize = 64 * 1024 * 1024;
constexpr uint32_t kExtPadding = 32 * 1024;
constexpr uint32_t kMio0HeaderLen = 16;

uint32_t ReadU32(const std::vector<uint8_t>& b, size_t at) {
    return ((uint32_t)b[at] << 24) | ((uint32_t)b[at + 1] << 16) | ((uint32_t)b[at + 2] << 8) | b[at + 3];
}

uint16_t ReadU16(const std::vector<uint8_t>& b, size_t at) {
    return (uint16_t)((b[at] << 8) | b[at + 1]);
}

void WriteU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = v >> 24;
    b[at + 1] = v >> 16;
    b[at + 2] = v >> 8;
    b[at + 3] = v;
}

void WriteU16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = v >> 8;
    b[at + 1] = v & 0xFF;
}

// A MIO0 block's data, and where its compressed and raw streams end relative to its start; empty if it isn't one.
std::vector<uint8_t> Mio0Decode(const std::vector<uint8_t>& buf, uint32_t off, uint32_t& end) {
    std::vector<uint8_t> out;
    if (off + kMio0HeaderLen > buf.size() || std::memcmp(&buf[off], "MIO0", 4) != 0) {
        return out;
    }
    const uint32_t destSize = ReadU32(buf, off + 4);
    const uint32_t compOff = ReadU32(buf, off + 8);
    const uint32_t uncompOff = ReadU32(buf, off + 12);
    uint32_t bit = 0, comp = 0, uncomp = 0;
    out.reserve(destSize);
    while (out.size() < destSize) {
        if (buf[off + kMio0HeaderLen + bit / 8] & (0x80 >> (bit % 8))) {
            out.push_back(buf[off + uncompOff + uncomp++]);
        } else {
            const uint8_t v0 = buf[off + compOff + comp];
            const uint8_t v1 = buf[off + compOff + comp + 1];
            comp += 2;
            const uint32_t dist = ((v0 & 0xF) << 8) + v1 + 1;
            for (int i = 0; i < (v0 >> 4) + 3; i++) {
                out.push_back(out[out.size() - dist]);
            }
        }
        bit++;
    }
    end = uncompOff + uncomp;
    return out;
}

// The address a lui and an addiu build.
uint32_t La2Int(const std::vector<uint8_t>& b, size_t lui, size_t addiu) {
    uint16_t hi = ReadU16(b, lui + 2);
    const uint16_t lo = ReadU16(b, addiu + 2);
    if (lo & 0x8000) {
        hi = (uint16_t)(hi - 1);
    }
    return ((uint32_t)hi << 16) | lo;
}

struct Block {
    uint32_t oldEnd = 0;
    int cmd = 0; // the level command that loads it, or 0xFF when code does
    uint32_t asmAt = 0;
    uint32_t a1 = 0;
    uint32_t newStart = 0;
    uint32_t newEnd = 0;
};

uint32_t ReadLE32(const std::vector<uint8_t>& b, size_t at) {
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | ((uint32_t)b[at + 3] << 24);
}

std::vector<uint8_t> ApplyPpf(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& ppf) {
    if (ppf.size() < 0x3C || std::memcmp(ppf.data(), "PPF30", 5) != 0 || ppf[5] != 2) {
        throw std::runtime_error("Only PPF 3.0 patches are supported");
    }
    const bool blockCheck = ppf[0x39] != 0;
    const bool undo = ppf[0x3A] != 0;
    size_t pos = 0x3C + (blockCheck ? 1024 : 0);
    size_t end = ppf.size();
    if (end >= 4 && std::memcmp(&ppf[end - 4], ".DIZ", 4) == 0) {
        end -= ReadLE32(ppf, end - 8) + 38;
    }
    std::vector<uint8_t> out = rom;
    while (pos + 9 <= end) {
        uint64_t off = 0;
        for (int i = 7; i >= 0; i--) {
            off = (off << 8) | ppf[pos + i];
        }
        const uint8_t n = ppf[pos + 8];
        if (off + n > out.size()) {
            out.resize(off + n, 0);
        }
        std::memcpy(&out[off], &ppf[pos + 9], n);
        pos += 9 + n * (undo ? 2 : 1);
    }
    return out;
}

std::vector<uint8_t> ApplyIps(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& ips) {
    std::vector<uint8_t> out = rom;
    size_t pos = 5;
    while (pos + 3 <= ips.size()) {
        if (std::memcmp(&ips[pos], "EOF", 3) == 0) {
            pos += 3;
            if (pos + 3 <= ips.size()) {
                out.resize((ips[pos] << 16) | (ips[pos + 1] << 8) | ips[pos + 2]);
            }
            return out;
        }
        const uint32_t off = (ips[pos] << 16) | (ips[pos + 1] << 8) | ips[pos + 2];
        uint32_t size = (ips[pos + 3] << 8) | ips[pos + 4];
        pos += 5;
        if (size == 0) {
            size = (ips[pos] << 8) | ips[pos + 1];
            if (off + size > out.size()) {
                out.resize(off + size, 0);
            }
            std::memset(&out[off], ips[pos + 2], size);
            pos += 3;
        } else {
            if (off + size > out.size()) {
                out.resize(off + size, 0);
            }
            std::memcpy(&out[off], &ips[pos], size);
            pos += size;
        }
    }
    throw std::runtime_error("IPS patch has no EOF");
}

uint64_t BpsNumber(const std::vector<uint8_t>& bps, size_t& pos) {
    uint64_t data = 0, shift = 1;
    while (true) {
        const uint8_t x = bps.at(pos++);
        data += (x & 0x7F) * shift;
        if (x & 0x80) {
            return data;
        }
        shift <<= 7;
        data += shift;
    }
}

std::vector<uint8_t> ApplyBps(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& bps) {
    size_t pos = 4;
    BpsNumber(bps, pos); // source size
    const uint64_t targetSize = BpsNumber(bps, pos);
    pos += BpsNumber(bps, pos); // metadata
    std::vector<uint8_t> out(targetSize, 0);
    size_t outPos = 0, sourceRel = 0, targetRel = 0;
    const size_t end = bps.size() - 12; // three CRC32s
    while (pos < end) {
        const uint64_t data = BpsNumber(bps, pos);
        const uint64_t command = data & 3;
        uint64_t length = (data >> 2) + 1;
        switch (command) {
            case 0: // SourceRead
                while (length--) {
                    out.at(outPos) = rom.at(outPos);
                    outPos++;
                }
                break;
            case 1: // TargetRead
                while (length--) {
                    out.at(outPos++) = bps.at(pos++);
                }
                break;
            case 2: { // SourceCopy
                const uint64_t d = BpsNumber(bps, pos);
                sourceRel += (d & 1 ? -1 : 1) * (int64_t)(d >> 1);
                while (length--) {
                    out.at(outPos++) = rom.at(sourceRel++);
                }
                break;
            }
            default: { // TargetCopy
                const uint64_t d = BpsNumber(bps, pos);
                targetRel += (d & 1 ? -1 : 1) * (int64_t)(d >> 1);
                while (length--) {
                    out.at(outPos++) = out.at(targetRel++);
                }
                break;
            }
        }
    }
    return out;
}

} // namespace

void FixN64Checksum(std::vector<uint8_t>& rom) {
    uint32_t t1, t2, t3, t4, t5, t6;
    t1 = t2 = t3 = t4 = t5 = t6 = 0xF8CA4DDC;
    for (size_t i = 0x1000; i < 0x101000; i += 4) {
        const uint32_t d = ReadU32(rom, i);
        if (t6 + d < t6) {
            t4++;
        }
        t6 += d;
        t3 ^= d;
        const uint32_t s = d & 0x1F;
        const uint32_t r = s ? (d << s) | (d >> (32 - s)) : d;
        t5 += r;
        t2 = t2 > d ? t2 ^ r : t2 ^ t6 ^ d;
        t1 += t5 ^ d;
    }
    WriteU32(rom, 0x10, t6 ^ t4 ^ t3);
    WriteU32(rom, 0x14, t5 ^ t2 ^ t1);
}

std::vector<uint8_t> SM64Extend(const std::vector<uint8_t>& rom) {
    if (rom.size() > 8 * 1024 * 1024) {
        throw std::runtime_error("sm64extend: the ROM is already extended");
    }
    const uint32_t inLen = (uint32_t)rom.size();
    std::vector<uint8_t> out = rom;
    out.resize(kExtSize, 0x01);

    std::map<uint32_t, Block> blocks;
    for (uint32_t a = kExtInStart; a + 4 <= inLen; a += 16) {
        if (std::memcmp(&rom[a], "MIO0", 4) == 0) {
            blocks[a] = Block{};
        }
    }
    for (uint32_t a = kExtInStart; a + 12 <= inLen; a += 4) {
        if ((rom[a] == 0x18 || rom[a] == 0x1A) && rom[a + 1] == 0x0C && rom[a + 2] == 0) {
            auto it = blocks.find(ReadU32(rom, a + 4));
            if (it != blocks.end()) {
                it->second.cmd = rom[a];
                it->second.oldEnd = ReadU32(rom, a + 8);
            }
        }
    }
    // lui a1 / lui a2 / addiu a2 / [addiu a1 | jal] pairs in code that load MIO0 blocks directly.
    for (uint32_t a = 0; a + 0x14 <= kExtInStart; a += 4) {
        if ((rom[a] & 0xFC) == 0x3C && (rom[a + 4] & 0xFC) == 0x3C && (rom[a + 8] & 0xFC) == 0x24) {
            uint32_t a1 = (rom[a + 0xC] & 0xFC) == 0x24 ? 0xC : ((rom[a + 0x10] & 0xFC) == 0x24 ? 0x10 : 0);
            if (a1 && (rom[a + 1] & 0x1F) == (rom[a + a1 + 1] & 0x1F) && (rom[a + 5] & 0x1F) == (rom[a + 9] & 0x1F)) {
                auto it = blocks.find(La2Int(rom, a, a + a1));
                if (it != blocks.end()) {
                    it->second.cmd = 0xFF;
                    it->second.asmAt = a;
                    it->second.a1 = a1;
                    it->second.newEnd = La2Int(rom, a + 4, a + 8);
                }
            }
        }
    }

    uint32_t outAddr = kExtOutStart;
    for (auto& [old, b] : blocks) {
        uint32_t end = 0;
        std::vector<uint8_t> data = Mio0Decode(rom, old, end);
        if (data.empty()) {
            continue;
        }
        uint32_t length = (uint32_t)data.size();
        if (b.cmd == 0x1A || b.cmd == 0xFF) {
            // Texture banks and code-loaded blocks stay MIO0, with every byte stored uncompressed.
            const uint32_t move = kMio0HeaderLen + (length + 7) / 8 + 2 + 2;
            std::memcpy(&out[outAddr], "MIO0", 4);
            WriteU32(out, outAddr + 4, length);
            WriteU32(out, outAddr + 8, move - 2);
            WriteU32(out, outAddr + 12, move);
            std::memset(&out[outAddr + kMio0HeaderLen], 0xFF, move - 2 - kMio0HeaderLen);
            out[outAddr + move - 2] = 0;
            out[outAddr + move - 1] = 0;
            std::memcpy(&out[outAddr + move], data.data(), length);
            length += move;
        } else {
            std::memcpy(&out[outAddr], data.data(), length);
            if (b.cmd == 0x18) {
                b.cmd = 0x17;
            }
        }
        if (b.oldEnd == 0) {
            b.oldEnd = old + end;
        }
        b.newStart = outAddr;
        b.newEnd = outAddr + length;
        outAddr += length + kExtPadding;
    }

    for (uint32_t a = kExtInStart; a + 12 <= inLen; a += 4) {
        if ((out[a] == 0x17 || out[a] == 0x18 || out[a] == 0x1A) && out[a + 1] == 0x0C && out[a + 2] < 2) {
            auto it = blocks.find(ReadU32(out, a + 4));
            if (it != blocks.end()) {
                WriteU32(out, a + 4, it->second.newStart);
                WriteU32(out, a + 8, it->second.newEnd);
                out[a] = (uint8_t)it->second.cmd;
            }
        }
    }
    for (auto& [old, b] : blocks) {
        if (b.cmd != 0xFF) {
            continue;
        }
        const uint32_t a = b.asmAt;
        const uint32_t vals[2][3] = { { b.newStart, a + 2, a + b.a1 + 2 }, { b.newEnd, a + 6, a + 0xA } };
        for (const auto& v : vals) {
            const uint16_t lo = v[0] & 0xFFFF;
            uint16_t hi = (v[0] >> 16) & 0xFFFF;
            if (lo & 0x8000) {
                hi = (uint16_t)(hi + 1);
            }
            WriteU16(out, v[1], hi);
            WriteU16(out, v[2], lo);
        }
    }
    FixN64Checksum(out);
    return out;
}

std::vector<uint8_t> Apply(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& patch) {
    if (patch.size() >= 5 && std::memcmp(patch.data(), "PPF30", 5) == 0) {
        return ApplyPpf(rom, patch);
    }
    if (patch.size() >= 5 && std::memcmp(patch.data(), "PATCH", 5) == 0) {
        return ApplyIps(rom, patch);
    }
    if (patch.size() >= 4 && std::memcmp(patch.data(), "BPS1", 4) == 0) {
        return ApplyBps(rom, patch);
    }
    throw std::runtime_error("Unknown patch format (PPF3, IPS and BPS are supported)");
}

} // namespace RomPatch
