#include "ConfigFactory.h"
#include "Companion.h"
#include "TinySHA1.hpp"
#include "binarytools/BinaryReader.h"
#include "spdlog/spdlog.h"
#include "utils/TorchUtils.h"
#include <algorithm>
#include <bk_zip/bk_unzip.h>
#include <cstring>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

namespace BK64 {

// Helpers

static uint16_t readBE16(const uint8_t* buf, uint32_t off) {
    return (buf[off] << 8) | buf[off + 1];
}

static uint32_t readBE32(const uint8_t* buf, uint32_t off) {
    return (buf[off] << 24) | (buf[off + 1] << 16) | (buf[off + 2] << 8) | buf[off + 3];
}

// Simple LE binary writer
struct BlobWriter {
    std::vector<char> data;

    void writeU8(uint8_t v) {
        data.push_back(static_cast<char>(v));
    }
    void writeU16(uint16_t v) {
        writeU8(v & 0xFF);
        writeU8((v >> 8) & 0xFF);
    }
    void writeU32(uint32_t v) {
        writeU16(v & 0xFFFF);
        writeU16((v >> 16) & 0xFFFF);
    }
    void writeS16(int16_t v) {
        writeU16(static_cast<uint16_t>(v));
    }

    // Write section header, return index to patch entry_count later
    size_t beginSection(ConfigSectionType type) {
        writeU16(static_cast<uint16_t>(type));
        size_t countPos = data.size();
        writeU16(0); // placeholder
        return countPos;
    }

    void patchU16(size_t pos, uint16_t val) {
        data[pos] = static_cast<char>(val & 0xFF);
        data[pos + 1] = static_cast<char>((val >> 8) & 0xFF);
    }
};

// BB overlay constant definitions

struct BBConfig {
    uint32_t offset;
    uint16_t vanilla;
    bool isByte;
};

static const BBConfig sBBConfigs[] = {
    { 254331, 0x85, true },     // NEW_GAME_MAP
    { 624378, 0x01, true },     // START_LEVEL_1
    { 625582, 0x69, true },     // START_LEVEL_2
    { 59470, 0xC3A0, false },   // KNOW_ALL_MOVES
    { 305126, 5, false },       // MUMBO_COST_TERMITE
    { 305134, 10, false },      // MUMBO_COST_CROC
    { 305142, 15, false },      // MUMBO_COST_WALRUS
    { 305150, 20, false },      // MUMBO_COST_PUMPKIN
    { 305158, 25, false },      // MUMBO_COST_BEE
    { 782878, 100, false },     // EGGS_NORMAL_MAX
    { 782910, 50, false },      // RED_FEATHERS_NORMAL_MAX
    { 782938, 10, false },      // GOLD_FEATHERS_NORMAL_MAX
    { 782870, 200, false },     // EGGS_CHEATO_MAX
    { 782902, 100, false },     // RED_FEATHERS_CHEATO_MAX
    { 782934, 20, false },      // GOLD_FEATHERS_CHEATO_MAX
    { 19162, 100, false },      // NOTES_MAX
    { 568414, 10, false },      // JIGGIES_PER_WORLD
    { 568490, 2, false },       // HONEYCOMBS_PER_WORLD
    { 568494, 6, false },       // EXTRA_HC_START
    { 0x986FA, 0x0112, false }, // WARP_EXIT_BANJOS_HOUSE
    { 0x98BAE, 0x6912, false }, // WARP_ENTER_LAIR
    { 568479, 0x0B, true },     // SPECIAL_LEVEL (vanilla: Spiral Mountain = 0x0B)
    { 570539, 0x0B, true },     // HIDE_JIGGIES_LEVEL (vanilla: Spiral Mountain = 0x0B)
    { 571219, 0x06, true },     // HIDE_COLLECTIBLES_LEVEL (vanilla: Grunty's Lair = 0x06)
};
static_assert(sizeof(sBBConfigs) / sizeof(sBBConfigs[0]) == static_cast<int>(CodeConstantKey::COUNT));

// Vanilla defaults for FCF698 data (lair overlay objects)

// D_8039347C - note door thresholds (12 doors)
static const uint16_t sVanillaNoteDoors[12] = { 50, 180, 260, 350, 450, 640, 765, 810, 828, 846, 864, 882 };

// D_803947F8 - jiggy puzzle costs (11 puzzles), first byte of each 4-byte entry
static const uint8_t sVanillaJiggyCosts[11] = { 1, 2, 5, 7, 8, 9, 10, 12, 15, 25, 4 };

// Vanilla level names from D_8036C58C (pause menu display)
static const char* sVanillaLevelNames[13] = { "GAME TOTAL",          "SPIRAL MOUNTAIN",     "GRUNTILDA'S LAIR",
                                              "MUMBO'S MOUNTAIN",    "TREASURE TROVE COVE", "CLANKER'S CAVERN",
                                              "BUBBLEGLOOP SWAMP",   "FREEZEEZY PEAK",      "GOBI'S VALLEY",
                                              "MAD MONSTER MANSION", "RUSTY BUCKET BAY",    "CLICK CLOCK WOOD",
                                              "STOP 'N' SWOP" };

// Find patched overlay via EOR boot code pointers

static uint8_t* FindPatchedOverlay(const std::vector<uint8_t>& rom, uint32_t& outSize, uint32_t& outRomAddr) {
    struct EORPair {
        uint32_t u;
        uint32_t l;
    };
    static const EORPair eorPairs[] = {
        { 0x107E, 0x1086 }, { 0x2872, 0x287A }, { 0x287E, 0x2886 }, { 0x288A, 0x2892 }, { 0x2896, 0x289A },
        { 0x28EA, 0x28F2 }, { 0x28F6, 0x28FE }, { 0x2902, 0x290A }, { 0x290E, 0x2912 },
    };

    for (const auto& pair : eorPairs) {
        uint16_t pLui = (rom[pair.u] << 8) | rom[pair.u + 1];
        uint16_t pAddiu = (rom[pair.l] << 8) | rom[pair.l + 1];
        uint32_t addr = (pLui << 16) + static_cast<int16_t>(pAddiu);

        if (addr == 0 || addr >= rom.size() - 6)
            continue;
        if (rom[addr] != 0x11 || rom[addr + 1] != 0x72)
            continue;

        uint32_t expectedLen = (rom[addr + 2] << 24) | (rom[addr + 3] << 16) | (rom[addr + 4] << 8) | rom[addr + 5];
        if (expectedLen < 800000 || expectedLen > 1000000)
            continue;

        try {
            uint32_t sz = 0x100000;
            uint8_t* result = bk_unzip(rom.data() + addr, &sz);
            if (result && sz == expectedLen) {
                outSize = sz;
                outRomAddr = addr;
                return result;
            }
            if (result)
                free(result);
        } catch (...) {}
    }

    outSize = 0;
    outRomAddr = 0;
    return nullptr;
}

// Find relocated F9CAE0

static uint8_t* FindRelocatedF9CAE0(const std::vector<uint8_t>& rom, uint32_t f37RomAddr, uint32_t& outSize) {
    for (uint32_t scan = f37RomAddr + 6; scan < f37RomAddr + 0x80000 && scan + 6 < rom.size(); scan++) {
        if (rom[scan] != 0x11)
            continue;
        if (rom[scan + 1] != 0x72)
            continue;

        uint32_t expected = (rom[scan + 2] << 24) | (rom[scan + 3] << 16) | (rom[scan + 4] << 8) | rom[scan + 5];
        if (expected < 80000 || expected > 100000)
            continue;

        try {
            uint32_t sz = 0x100000;
            uint8_t* result = bk_unzip(rom.data() + scan, &sz);
            if (result && sz == expected) {
                SPDLOG_INFO("[ConfigFactory] Found relocated F9CAE0 at ROM 0x{:X} ({} bytes)", scan, sz);
                outSize = sz;
                return result;
            }
            if (result)
                free(result);
        } catch (...) {}

        break;
    }

    outSize = 0;
    return nullptr;
}

static constexpr uint32_t FCF698_ROM_OFFSET = 0x3F5CDB0;
static constexpr uint32_t FCF698_SIZE = 9888;
static constexpr uint32_t FCF698_NOTE_DOORS_OFF = 1996;
static constexpr uint32_t FCF698_JIGGY_PUZZLES_OFF = 6984;

static bool ScanWarpDestDiff(const uint8_t* vanOvl, const uint8_t* modOvl, uint32_t funcOff, uint32_t funcLen,
                             uint32_t ovlSize, int& outVanDest, int& outModDest) {
    static const uint8_t kEpilogue[16] = {
        0x8F, 0xBF, 0x00, 0x14, // LW $ra, 0x14($sp)
        0x27, 0xBD, 0x00, 0x18, // ADDIU $sp, $sp, 0x18
        0x03, 0xE0, 0x00, 0x08, // JR $ra
        0x00, 0x00, 0x00, 0x00  // NOP
    };

    outVanDest = -1;
    outModDest = -1;
    uint32_t limit = std::min<uint32_t>(funcLen, 200);
    if (funcOff + limit > ovlSize || limit < 20) {
        return false;
    }

    const uint8_t* van = vanOvl + funcOff;
    const uint8_t* mod = modOvl + funcOff;
    for (uint32_t i = 0; i + 20 <= limit; i += 4) {
        if ((van[i] == 0x24 || van[i] == 0x34) && van[i + 1] == 0x05 &&
            memcmp(van + i + 4, kEpilogue, 16) == 0) {
            // Same load must still be present in the modified overlay.
            if ((mod[i] == 0x24 || mod[i] == 0x34) && mod[i + 1] == 0x05) {
                int vanDest = (van[i + 2] << 8) | van[i + 3];
                int modDest = (mod[i + 2] << 8) | mod[i + 3];
                if (vanDest != modDest) {
                    outVanDest = vanDest;
                    outModDest = modDest;
                    return true;
                }
            }
            i += 16; // skip the epilogue; a conditional warp may have another site after it
            continue;
        }
        // Bare JR $ra outside a matched site: end of this warp stub.
        if (van[i] == 0x03 && van[i + 1] == 0xE0 && van[i + 2] == 0x00 && van[i + 3] == 0x08) {
            return false;
        }
    }
    return false;
}

// Custom-code blob detection
//
// Some hacks inject their own MIPS code at ROM 0x3F00000; the boot loader copies it up to
// high RAM (0x80780000 for some hacks, 0x80400000 for others, e.g. The Jiggies of Time)
// and the hack rewrites a few vanilla `jal`s to jump into it. Lighthouse runs the
// decompiled C, not the original MIPS, so none of that survives at runtime. The hack's
// scripted behavior just quietly never happens.
//
// We can at least spot it and warn, so users aren't left wondering why a hack feels
// half-broken on the port. Both signals have to fire; either one alone false-positives on
// stray data or BB's own overlay relocations:
//   (a) a believable MIPS prologue (`27BD FFxx`) in the first 0x100 bytes at 0x3F00000, and
//   (b) a handful of `jal` self-calls into [0x80400000, 0x80800000) inside the blob -
//       real code calls itself constantly, random data doesn't.
//
// The blob is stored either rzip-compressed (header `0x11 0x72`) somewhere in high ROM,
// or as raw uncompressed MIPS at exactly 0x3F00000 that the boot stub DMA-copies verbatim.

static constexpr uint32_t CUSTOM_CODE_ROM_OFFSET = 0x3F00000;
static constexpr uint32_t CUSTOM_CODE_RAM_BASE = 0x80400000;
static constexpr uint32_t CUSTOM_CODE_RAM_END = 0x80800000;
static constexpr uint32_t CUSTOM_CODE_RAW_MAX_SIZE = 0x100000; // raw-blob window before 0xFF trim
static constexpr int CUSTOM_CODE_MIN_INTERNAL_JALS = 4;    // self-call threshold

static bool LooksLikeMipsFunctionPrologue(const uint8_t* p) {
    // addiu $sp, $sp, -N  ->  0x27 0xBD 0xFF 0xxx
    return p[0] == 0x27 && p[1] == 0xBD && p[2] == 0xFF;
}

// Count `jal` instructions in [start, start+size) whose target lies in
// [CUSTOM_CODE_RAM_BASE, CUSTOM_CODE_RAM_END). Returns the count and, via
// outFirstTarget, the first such target seen (for logging).
static int CountInternalCustomCodeJals(const std::vector<uint8_t>& rom, uint32_t start, uint32_t size,
                                       uint32_t& outFirstTarget) {
    outFirstTarget = 0;
    int count = 0;
    uint32_t end = start + size;
    if (end > rom.size())
        end = static_cast<uint32_t>(rom.size());
    for (uint32_t off = start; off + 4 <= end; off += 4) {
        if (rom[off] != 0x0C)
            continue; // op=3 (JAL) starts with 0x0C in BE
        uint32_t word = (uint32_t(rom[off]) << 24) | (uint32_t(rom[off + 1]) << 16) | (uint32_t(rom[off + 2]) << 8) |
                        uint32_t(rom[off + 3]);
        if ((word >> 26) != 0x03)
            continue;
        uint32_t target = 0x80000000u | ((word & 0x03FFFFFFu) << 2);
        if (target >= CUSTOM_CODE_RAM_BASE && target < CUSTOM_CODE_RAM_END) {
            if (count == 0)
                outFirstTarget = target;
            count++;
        }
    }
    return count;
}

// Run the two custom-code signals against a candidate payload. On a hit, fills the
// out params (SHA1 over the full payload; ramBase = first internal jal target rounded
// down to 1 MB, which recovers the boot stub's load address closely enough for logging
// and identification) and returns true.
static bool CheckCustomCodePayload(const std::vector<uint8_t>& payload, int& outInternalJalCount,
                                   uint32_t& outFirstInternalTarget, uint32_t& outRamBase, uint8_t outBlobSha1[20]) {
    bool hasPrologue = false;
    uint32_t prologueScan = std::min<uint32_t>(static_cast<uint32_t>(payload.size()), 0x100);
    for (uint32_t p = 0; p + 4 <= prologueScan; p += 4) {
        if (LooksLikeMipsFunctionPrologue(payload.data() + p)) {
            hasPrologue = true;
            break;
        }
    }
    if (!hasPrologue) {
        return false;
    }

    uint32_t firstTarget = 0;
    int jals = CountInternalCustomCodeJals(payload, 0, static_cast<uint32_t>(payload.size()), firstTarget);
    if (jals < CUSTOM_CODE_MIN_INTERNAL_JALS) {
        return false;
    }

    outInternalJalCount = jals;
    outFirstInternalTarget = firstTarget;
    outRamBase = firstTarget & ~0xFFFFFu;
    if (outBlobSha1) {
        sha1::SHA1 s;
        s.processBytes(payload.data(), payload.size());
        uint32_t digest[5];
        s.getDigest(digest);
        for (int i = 0; i < 5; i++) {
            outBlobSha1[i * 4 + 0] = (digest[i] >> 24) & 0xFF;
            outBlobSha1[i * 4 + 1] = (digest[i] >> 16) & 0xFF;
            outBlobSha1[i * 4 + 2] = (digest[i] >> 8) & 0xFF;
            outBlobSha1[i * 4 + 3] = digest[i] & 0xFF;
        }
    }
    return true;
}

// On success, also writes the decompressed-payload SHA1 to outBlobSha1.
// Hash is written to cleanly identify the particular blob so Lighthouse can
// act on it.
static bool DetectCustomCodeBlob(const std::vector<uint8_t>& rom, uint32_t& outBlobRomOffset, int& outInternalJalCount,
                                 uint32_t& outFirstInternalTarget, uint32_t& outRamBase, CustomCodeKind& outKind,
                                 uint8_t outBlobSha1[20]) {
    outBlobRomOffset = 0;
    outInternalJalCount = 0;
    outFirstInternalTarget = 0;
    outRamBase = 0;
    outKind = CustomCodeKind::NONE;
    if (outBlobSha1) {
        std::memset(outBlobSha1, 0, 20);
    }

    if (rom.size() < CUSTOM_CODE_ROM_OFFSET) {
        return false;
    }

    // Walk every rzip blob in the high-ROM region and check if
    // its decompressed contents look like the custom-code blob.
    for (uint32_t off = CUSTOM_CODE_ROM_OFFSET; off + 6 <= rom.size(); off += 4) {
        if (rom[off] != 0x11 || rom[off + 1] != 0x72) {
            continue;
        }
        uint32_t expectedSize = (uint32_t(rom[off + 2]) << 24) | (uint32_t(rom[off + 3]) << 16) |
                                (uint32_t(rom[off + 4]) << 8) | uint32_t(rom[off + 5]);
        // Plausible size for an injected custom-code blob: ~10 KB to ~1 MB.
        if (expectedSize < 0x4000 || expectedSize > 0x100000) {
            continue;
        }

        uint8_t* unzipped = nullptr;
        uint32_t unzippedSize = expectedSize;
        try {
            unzipped = bk_unzip(rom.data() + off, &unzippedSize);
        } catch (...) { continue; }
        if (!unzipped || unzippedSize != expectedSize) {
            if (unzipped)
                free(unzipped);
            continue;
        }

        std::vector<uint8_t> payload(unzipped, unzipped + unzippedSize);
        free(unzipped);
        if (!CheckCustomCodePayload(payload, outInternalJalCount, outFirstInternalTarget, outRamBase, outBlobSha1)) {
            continue;
        }
        outBlobRomOffset = off;
        outKind = CustomCodeKind::BB_INJECTED;
        return true;
    }

    // No compressed blob found; try a raw uncompressed blob at 0x3F00000.
    if (rom.size() > CUSTOM_CODE_ROM_OFFSET + 4) {
        uint32_t windowEnd =
            static_cast<uint32_t>(std::min<size_t>(rom.size(), CUSTOM_CODE_ROM_OFFSET + CUSTOM_CODE_RAW_MAX_SIZE));
        uint32_t rawSize = windowEnd - CUSTOM_CODE_ROM_OFFSET;
        while (rawSize > 0 && rom[CUSTOM_CODE_ROM_OFFSET + rawSize - 1] == 0xFF) {
            rawSize--;
        }
        if (rawSize >= 0x4000) {
            std::vector<uint8_t> payload(rom.begin() + CUSTOM_CODE_ROM_OFFSET,
                                         rom.begin() + CUSTOM_CODE_ROM_OFFSET + rawSize);
            if (CheckCustomCodePayload(payload, outInternalJalCount, outFirstInternalTarget, outRamBase,
                                       outBlobSha1)) {
                outBlobRomOffset = CUSTOM_CODE_ROM_OFFSET;
                outKind = CustomCodeKind::BB_GLOBALIZATION;
                return true;
            }
        }
    }

    return false;
}

// Decompressed size of the vanilla US rev0 F37F90 code overlay. BB's in-place
// constant patching never changes it; a different size means the hack rebuilt
// its code overlay (e.g. decomp-built) and every fixed overlay offset is invalid.
static constexpr uint32_t VANILLA_OVERLAY_SIZE = 902656;
static constexpr uint32_t VANILLA_F37F90_OFFSET = 0xF37F90;

RomhackKind ClassifyRomhack(const std::vector<uint8_t>& rom) {
    if (rom.size() <= 0x1000000) {
        return RomhackKind::NotRomhack;
    }

    // No BK boot overlay reachable via the EOR pointers means this probably isn't a
    // BK-derived ROM at all.
    uint32_t ovlSize = 0, ovlAddr = 0;
    uint8_t* ovl = FindPatchedOverlay(rom, ovlSize, ovlAddr);
    if (ovl == nullptr) {
        return RomhackKind::UnknownRom;
    }
    free(ovl);

    // The patched overlay's decompressed size matches vanilla exactly.
    if (ovlSize != VANILLA_OVERLAY_SIZE) {
        return RomhackKind::CustomBuild;
    }

    // The vanilla sub-16MB region is intact.
    if (rom.size() < VANILLA_F37F90_OFFSET + 6 || rom[VANILLA_F37F90_OFFSET] != 0x11 ||
        rom[VANILLA_F37F90_OFFSET + 1] != 0x72 ||
        readBE32(rom.data(), VANILLA_F37F90_OFFSET + 2) != VANILLA_OVERLAY_SIZE) {
        return RomhackKind::CustomBuild;
    }

    return RomhackKind::BBRomhack;
}

// Public wrapper: thin bool-returning view used by Lighthouse to gate UI before
// running the rest of extraction. Same detection logic as DetectCustomCodeBlob
// above; we discard the returned metadata for callers that just want a yes/no.
bool GetCustomCodeBlobInfo(const std::vector<uint8_t>& rom, CustomCodeKind& outKind, char outSha1Hex[41]) {
    uint32_t blobRom = 0, firstTarget = 0, ramBase = 0;
    int jalCount = 0;
    uint8_t sha1[20] = {};
    outKind = CustomCodeKind::NONE;
    outSha1Hex[0] = '\0';
    if (!DetectCustomCodeBlob(rom, blobRom, jalCount, firstTarget, ramBase, outKind, sha1)) {
        return false;
    }
    for (int i = 0; i < 20; i++) {
        std::snprintf(outSha1Hex + i * 2, 3, "%02x", sha1[i]);
    }
    return true;
}

bool HasCustomCodeBlob(const std::vector<uint8_t>& rom) {
    uint32_t blobRom = 0, firstTarget = 0, ramBase = 0;
    int jalCount = 0;
    CustomCodeKind kind = CustomCodeKind::NONE;
    if (DetectCustomCodeBlob(rom, blobRom, jalCount, firstTarget, ramBase, kind, nullptr)) {
        return kind == CustomCodeKind::BB_INJECTED;
    }
    return ClassifyRomhack(rom) == RomhackKind::CustomBuild;
}

// Main entry point

std::vector<char> BuildGameConfigBlob(const std::vector<uint8_t>& rom, const std::string& romName) {
    const RomhackKind kind = ClassifyRomhack(rom);
    if (kind == RomhackKind::NotRomhack || kind == RomhackKind::UnknownRom) {
        return {};
    }

    // Look for injected custom MIPS code.
    uint32_t customCodeBlobRom = 0;
    uint32_t customCodeFirstTarget = 0;
    uint32_t customCodeRamBase = 0;
    int customCodeJalCount = 0;
    CustomCodeKind customCodeKind = CustomCodeKind::NONE;
    uint8_t customCodeSha1[20] = {};
    bool hasCustomCode = DetectCustomCodeBlob(rom, customCodeBlobRom, customCodeJalCount, customCodeFirstTarget,
                                              customCodeRamBase, customCodeKind, customCodeSha1);
    if (hasCustomCode) {
        char hex[41];
        for (int i = 0; i < 20; i++) {
            std::snprintf(hex + i * 2, 3, "%02x", customCodeSha1[i]);
        }
        if (customCodeKind == CustomCodeKind::BB_INJECTED) {
            SPDLOG_WARN("[ConfigFactory] Injected custom MIPS code detected at ROM 0x{:X}; {} internal jal "
                        "self-references seen, first target 0x{:08X}; sha1={}.",
                        customCodeBlobRom, customCodeJalCount, customCodeFirstTarget, hex);
            SPDLOG_WARN("[ConfigFactory] This romhack ships custom code.  Lighthouse runs the decompiled C "
                        "source and cannot execute injected code, so scripted behavior driven by that code "
                        "will be silently absent until a hand-port is implemented.");
        } else {
            SPDLOG_INFO("[ConfigFactory] BB globalized-overlay blob at ROM 0x{:X} ({} internal jals, "
                        "ramBase 0x{:08X}); sha1={}.",
                        customCodeBlobRom, customCodeJalCount, customCodeRamBase, hex);
        }
    }

    // Find patched overlay
    uint32_t overlaySize = 0, f37RomAddr = 0;
    uint8_t* overlay = nullptr;
    if (kind == RomhackKind::BBRomhack) {
        overlay = FindPatchedOverlay(rom, overlaySize, f37RomAddr);
        if (!overlay)
            return {};
        SPDLOG_INFO("[ConfigFactory] Found patched overlay at ROM 0x{:X} ({} bytes)", f37RomAddr, overlaySize);
    } else {
        SPDLOG_WARN("[ConfigFactory] This romhack was not built with Banjo's Backpack and must be hand-ported.");
    }

    BlobWriter blob;
    uint16_t sectionCount = 0;

    // Write header (magic + version + placeholder section count + romName)
    blob.writeU32(GAMECONFIG_MAGIC);
    blob.writeU16(GAMECONFIG_VERSION);
    size_t sectionCountPos = blob.data.size();
    blob.writeU16(0); // placeholder

    // Write rom name (u8 len + chars, no null terminator)
    uint8_t nameLen = static_cast<uint8_t>(std::min(romName.size(), size_t(255)));
    blob.writeU8(nameLen);
    for (uint8_t i = 0; i < nameLen; i++) {
        blob.writeU8(static_cast<uint8_t>(romName[i]));
    }

    // Section: CUSTOM_CODE
    if (hasCustomCode) {
        // entry_count is informational here; the section always carries a single
        // {ramBase, sha1[20]} payload. Use 1 so anyone walking entries lands sanely.
        size_t secPos = blob.beginSection(ConfigSectionType::CUSTOM_CODE);
        blob.writeU32(customCodeRamBase);
        for (int i = 0; i < 20; i++) {
            blob.writeU8(customCodeSha1[i]);
        }
        blob.patchU16(secPos, 1);
        sectionCount++;

        // Section: CUSTOM_CODE_INFO
        size_t infoPos = blob.beginSection(ConfigSectionType::CUSTOM_CODE_INFO);
        blob.writeU16(static_cast<uint16_t>(customCodeKind));
        blob.writeU16(0); // pad
        blob.patchU16(infoPos, 1);
        sectionCount++;
    }

    // Section: ROM_HASH (data-only romhack identifier)
    {
        std::string romHash = Companion::Instance->GetCartridge()->GetHash();
        // GetHash() returns 40 lowercase hex chars; convert back to 20 raw bytes.
        if (romHash.size() == 40) {
            size_t secPos = blob.beginSection(ConfigSectionType::ROM_HASH);
            for (int i = 0; i < 20; i++) {
                uint8_t byte = 0;
                for (int n = 0; n < 2; n++) {
                    char c = romHash[i * 2 + n];
                    uint8_t nib = (c >= '0' && c <= '9')   ? c - '0'
                                  : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                                  : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                                           : 0;
                    byte = (byte << 4) | nib;
                }
                blob.writeU8(byte);
            }
            blob.patchU16(secPos, 1);
            sectionCount++;
            SPDLOG_INFO("[ConfigFactory] ROM_HASH = {}", romHash);
        } else {
            SPDLOG_WARN("[ConfigFactory] ROM hash unexpected length {}, skipping ROM_HASH section", romHash.size());
        }
    }

    // Section: CODE_CONSTANTS
    if (overlay != nullptr) {
        size_t secPos = blob.beginSection(ConfigSectionType::CODE_CONSTANTS);
        uint16_t count = 0;

        for (int i = 0; i < static_cast<int>(CodeConstantKey::COUNT); i++) {
            const auto& entry = sBBConfigs[i];
            if (entry.offset + 2 > overlaySize)
                continue;

            uint16_t val = entry.isByte ? overlay[entry.offset] : readBE16(overlay, entry.offset);
            if (val != entry.vanilla) {
                blob.writeU16(static_cast<uint16_t>(i));
                blob.writeU16(val);
                count++;
                SPDLOG_INFO("[ConfigFactory] code_const[{}] = {} (vanilla: {})", i, val, entry.vanilla);
            }
        }

        if (count > 0) {
            blob.patchU16(secPos, count);
            sectionCount++;
        } else {
            blob.data.resize(secPos - 2); // remove empty section header
        }
    }

    // F9CAE0 sections
    if (f37RomAddr > 0) {
        uint32_t modSize = 0, vanSize = 0x100000;
        uint8_t* modF9 = FindRelocatedF9CAE0(rom, f37RomAddr, modSize);

        // Vanilla F9CAE0 at original location
        uint8_t* vanF9 = nullptr;
        static constexpr uint32_t VANILLA_F9CAE0 = 0xF9CAE0;
        if (VANILLA_F9CAE0 + 6 < rom.size() && rom[VANILLA_F9CAE0] == 0x11 && rom[VANILLA_F9CAE0 + 1] == 0x72) {
            try {
                vanF9 = bk_unzip(rom.data() + VANILLA_F9CAE0, &vanSize);
            } catch (...) {}
        }

        if (modF9 && vanF9 && modSize == vanSize) {
            uint32_t f9Size = modSize;

            // Section 2: SCENE_REMAP (D_8036B810 at offset 0x8288)
            if (f9Size > 0x8288 + 155 * 8) {
                size_t secPos = blob.beginSection(ConfigSectionType::SCENE_REMAP);
                uint16_t count = 0;

                for (uint32_t off = 0x8288; off + 4 <= f9Size; off += 8) {
                    uint16_t mapId = readBE16(modF9, off);
                    if (mapId == 0 && off > 0x8288 + 8)
                        break;
                    if (mapId == 0)
                        continue;

                    uint16_t modLevel = readBE16(modF9, off + 2);
                    uint16_t vanLevel = readBE16(vanF9, off + 2);
                    if (modLevel != vanLevel) {
                        blob.writeU16(mapId);
                        blob.writeU16(modLevel);
                        count++;
                        SPDLOG_INFO("[ConfigFactory] scene_remap: map 0x{:X} level {} -> {}", mapId, vanLevel,
                                    modLevel);
                    }
                }

                if (count > 0) {
                    blob.patchU16(secPos, count);
                    sectionCount++;
                } else {
                    blob.data.resize(secPos - 2);
                }
            }

            // Section 3: RETURN_TO_LAIR (D_8036C560 at offset 0x8FD0)
            if (f9Size > 0x8FD0 + 11 * 4) {
                size_t secPos = blob.beginSection(ConfigSectionType::RETURN_TO_LAIR);
                uint16_t count = 0;

                for (int i = 0; i < 11; i++) {
                    uint32_t off = 0x8FD0 + i * 4;
                    uint16_t modMap = readBE16(modF9, off);
                    uint16_t modExit = readBE16(modF9, off + 2);
                    uint16_t vanMap = readBE16(vanF9, off);
                    uint16_t vanExit = readBE16(vanF9, off + 2);
                    if (modMap != vanMap || modExit != vanExit) {
                        blob.writeU8(static_cast<uint8_t>(i));
                        blob.writeU8(0); // pad
                        blob.writeU16(modMap);
                        blob.writeU16(modExit);
                        count++;
                        SPDLOG_INFO("[ConfigFactory] return_to_lair[{}]: 0x{:X}/0x{:X} -> "
                                    "0x{:X}/0x{:X}",
                                    i, vanMap, vanExit, modMap, modExit);
                    }
                }

                if (count > 0) {
                    blob.patchU16(secPos, count);
                    sectionCount++;
                } else {
                    blob.data.resize(secPos - 2);
                }
            }

            // Section 4: MUSIC (offset 0xA8F0)
            if (f9Size > 0xA8F0 + 8) {
                size_t secPos = blob.beginSection(ConfigSectionType::MUSIC);
                uint16_t count = 0;

                for (uint32_t off = 0xA8F0; off + 8 <= f9Size; off += 8) {
                    uint16_t mapId = readBE16(modF9, off);
                    if (mapId == 0 && off > 0xA8F0 + 8)
                        break;
                    if (mapId == 0)
                        continue;

                    bool differs = memcmp(modF9 + off, vanF9 + off, 8) != 0;
                    if (differs) {
                        blob.writeU16(mapId);
                        blob.writeU16(readBE16(modF9, off + 2));
                        blob.writeU16(readBE16(modF9, off + 4));
                        count++;
                    }
                }

                if (count > 0) {
                    blob.patchU16(secPos, count);
                    sectionCount++;
                } else {
                    blob.data.resize(secPos - 2);
                }
            }

            // Section 5: SKYBOX (offset 0x87B0, 40-byte entries)
            // Full 3-layer extraction: scene_id + 3x {model_id, scale(f32 as LE u32),
            // rotation(f32 as LE u32)} MapSkyInfo layout in F9CAE0 (MIPS BE, 40
            // bytes):
            //   +0:  s16 map (BE)
            //   +4:  s16 model1 (BE), +8: f32 scale1 (BE), +12: f32 rot1 (BE)
            //   +16: s16 model2 (BE), +20: f32 scale2 (BE), +24: f32 rot2 (BE)
            //   +28: s16 model3 (BE), +32: f32 scale3 (BE), +36: f32 rot3 (BE)
            if (f9Size > 0x87B0 + 40) {
                size_t secPos = blob.beginSection(ConfigSectionType::SKYBOX);
                uint16_t count = 0;

                for (uint32_t off = 0x87B0; off + 40 <= f9Size; off += 40) {
                    uint16_t sceneId = readBE16(modF9, off);
                    if (sceneId == 0 && off > 0x87B0 + 40)
                        break;
                    if (sceneId == 0)
                        continue;

                    bool differs = memcmp(modF9 + off, vanF9 + off, 40) != 0;
                    if (differs) {
                        blob.writeU16(sceneId);
                        // 3 layers: model (s16 LE), scale (f32 as u32 LE), rotation (f32 as
                        // u32 LE)
                        for (int layer = 0; layer < 3; layer++) {
                            uint32_t base = off + 4 + layer * 12;
                            blob.writeS16(static_cast<int16_t>(readBE16(modF9, base))); // model_id
                            blob.writeU32(readBE32(modF9, base + 4));                   // scale (BE f32 bits -> LE u32)
                            blob.writeU32(readBE32(modF9, base + 8)); // rotation (BE f32 bits -> LE u32)
                        }
                        count++;
                        SPDLOG_INFO("[ConfigFactory] skybox: scene 0x{:X} models [{},{},{}]", sceneId,
                                    readBE16(modF9, off + 4), readBE16(modF9, off + 16), readBE16(modF9, off + 28));
                    }
                }

                if (count > 0) {
                    blob.patchU16(secPos, count);
                    sectionCount++;
                } else {
                    blob.data.resize(secPos - 2);
                }
            }

            // Section 6: SCENE_DEF (offset 0x7650, 24-byte MIPS entries)
            // MapModelDescription: {s16 map, s16 opa, s16 xlu, s16 min[3], s16
            // max[3], pad[2], f32 scale}
            if (f9Size > 0x7650 + 24) {
                size_t secPos = blob.beginSection(ConfigSectionType::SCENE_DEF);
                uint16_t count = 0;

                for (uint32_t off = 0x7650; off + 24 <= f9Size; off += 24) {
                    uint16_t sceneId = readBE16(modF9, off);
                    if (sceneId == 0 && off > 0x7650 + 24)
                        break;
                    if (sceneId == 0)
                        continue;

                    bool differs = memcmp(modF9 + off, vanF9 + off, 24) != 0;
                    if (differs) {
                        blob.writeU16(sceneId);
                        blob.writeS16(static_cast<int16_t>(readBE16(modF9, off + 2))); // opa
                        blob.writeS16(static_cast<int16_t>(readBE16(modF9, off + 4))); // xlu
                        for (int b = 0; b < 3; b++) {
                            blob.writeS16(static_cast<int16_t>(readBE16(modF9, off + 6 + b * 2))); // min[3]
                        }
                        for (int b = 0; b < 3; b++) {
                            blob.writeS16(static_cast<int16_t>(readBE16(modF9, off + 12 + b * 2))); // max[3]
                        }
                        blob.writeU32(readBE32(modF9, off + 20)); // scale (BE f32 bits -> LE u32)
                        count++;
                    }
                }

                if (count > 0) {
                    blob.patchU16(secPos, count);
                    sectionCount++;
                } else {
                    blob.data.resize(secPos - 2);
                }
            }

            // Section 9: LEVEL_NAMES (F9CAE0 offset 86068-86302, null-terminated
            // ASCII)
            if (f9Size > 86302) {
                size_t secPos = blob.beginSection(ConfigSectionType::LEVEL_NAMES);
                uint16_t count = 0;

                // Parse the same way BB does: scan for null-terminated strings
                int nameIdx = 0;
                uint32_t nameStart = 0;
                for (uint32_t off = 86068; off < 86302 && nameIdx < 13; off++) {
                    if (modF9[off] == 0) {
                        if (nameStart > 0) {
                            // Compare against vanilla
                            uint32_t len = off - nameStart;
                            std::string modName(reinterpret_cast<char*>(modF9 + nameStart), len);
                            bool differs = (nameIdx >= 13) || (modName != sVanillaLevelNames[nameIdx]);
                            if (differs) {
                                blob.writeU8(static_cast<uint8_t>(nameIdx));
                                blob.writeU8(static_cast<uint8_t>(len));
                                for (uint32_t c = 0; c < len; c++) {
                                    blob.writeU8(modF9[nameStart + c]);
                                }
                                count++;
                                SPDLOG_INFO("[ConfigFactory] level_name[{}] = \"{}\"", nameIdx, modName);
                            }
                            nameIdx++;
                            nameStart = 0;
                        }
                    } else {
                        if (nameStart == 0) {
                            nameStart = off;
                        }
                    }
                }

                if (count > 0) {
                    blob.patchU16(secPos, count);
                    sectionCount++;
                } else {
                    blob.data.resize(secPos - 2);
                }
            }
            // Section 10: WARP_DESTINATIONS
            // BB stores a warp function pointer table in F9CAE0 at offset 0xC3F0
            // (558 entries, 4 bytes each). Each points to a warp function in the
            // F37F90 code overlay. Decompress the vanilla overlay from its
            // original ROM location, scan both copies for the ADDIU $a1 dest
            // pattern, and emit diffs.
            {
                static constexpr uint32_t WARP_TABLE_OFF = 0xC3F0; // 50160 decimal
                static constexpr int WARP_ENTRY_COUNT = 558;       // (52392 - 50160) / 4
                // BB base: 0x80000000 + 2650000 (Form1.cs line 2538)
                static constexpr uint32_t N64_OVL_BASE = 0x80000000U + 2650000U;

                uint8_t* vanOvl = nullptr;
                uint32_t vanOvlSize = 0x100000;
                if (VANILLA_F37F90_OFFSET + 6 < rom.size() && rom[VANILLA_F37F90_OFFSET] == 0x11 &&
                    rom[VANILLA_F37F90_OFFSET + 1] == 0x72) {
                    try {
                        vanOvl = bk_unzip(rom.data() + VANILLA_F37F90_OFFSET, &vanOvlSize);
                    } catch (...) {}
                }

                if (vanOvl && f9Size > WARP_TABLE_OFF + WARP_ENTRY_COUNT * 4) {
                    size_t secPos = blob.beginSection(ConfigSectionType::WARP_DESTINATIONS);
                    uint16_t count = 0;

                    // Sorted unique function starts, so each scan can be bounded by the
                    // next table-referenced function instead of a blind 200-byte window.
                    std::vector<uint32_t> funcOffs;
                    funcOffs.reserve(WARP_ENTRY_COUNT);
                    for (int w = 0; w < WARP_ENTRY_COUNT; w++) {
                        uint32_t addr = readBE32(vanF9, WARP_TABLE_OFF + w * 4);
                        if (addr >= N64_OVL_BASE) {
                            funcOffs.push_back(addr - N64_OVL_BASE);
                        }
                    }
                    std::sort(funcOffs.begin(), funcOffs.end());
                    funcOffs.erase(std::unique(funcOffs.begin(), funcOffs.end()), funcOffs.end());

                    uint32_t scanSize = std::min(vanOvlSize, overlaySize);
                    for (int w = 0; w < WARP_ENTRY_COUNT; w++) {
                        uint32_t tOff = WARP_TABLE_OFF + w * 4;
                        uint32_t addr = readBE32(vanF9, tOff);
                        if (addr < N64_OVL_BASE) {
                            continue;
                        }
                        uint32_t funcOff = addr - N64_OVL_BASE;

                        auto next = std::upper_bound(funcOffs.begin(), funcOffs.end(), funcOff);
                        uint32_t funcLen = (next != funcOffs.end()) ? (*next - funcOff) : 200;

                        int vanDest = -1, modDest = -1;
                        if (ScanWarpDestDiff(vanOvl, overlay, funcOff, funcLen, scanSize, vanDest, modDest)) {
                            blob.writeU16(static_cast<uint16_t>(w));
                            blob.writeU16(static_cast<uint16_t>(modDest));
                            count++;
                            SPDLOG_INFO("[ConfigFactory] warp[{}]: 0x{:04X} -> 0x{:04X}", w, vanDest, modDest);
                        }
                    }

                    if (count > 0) {
                        blob.patchU16(secPos, count);
                        sectionCount++;
                    } else {
                        blob.data.resize(secPos - 2);
                    }
                } else if (!vanOvl) {
                    SPDLOG_WARN("[ConfigFactory] Could not decompress vanilla F37F90 overlay for warp diff");
                }

                if (vanOvl) {
                    free(vanOvl);
                }
            }
        } else if (modF9 && vanF9) {
            SPDLOG_WARN("[ConfigFactory] F9CAE0 size mismatch: modified={} vanilla={}", modSize, vanSize);
        }

        if (modF9)
            free(modF9);
        if (vanF9)
            free(vanF9);
    }

    free(overlay);

    // FCF698 sections (lair overlay objects, uncompressed in globalized blob)
    if (kind == RomhackKind::BBRomhack && rom.size() > FCF698_ROM_OFFSET + FCF698_SIZE) {
        const uint8_t* fcf = rom.data() + FCF698_ROM_OFFSET;

        // Validate: first note door value should be a reasonable u16 (0-10000)
        uint16_t firstDoor = readBE16(fcf, FCF698_NOTE_DOORS_OFF);
        if (firstDoor <= 10000) {
            // Section 7: NOTE_DOORS
            {
                size_t secPos = blob.beginSection(ConfigSectionType::NOTE_DOORS);
                uint16_t count = 0;

                for (int i = 0; i < 12; i++) {
                    uint16_t val = readBE16(fcf, FCF698_NOTE_DOORS_OFF + i * 2);
                    if (val != sVanillaNoteDoors[i]) {
                        blob.writeU8(static_cast<uint8_t>(i));
                        blob.writeU8(0); // pad
                        blob.writeU16(val);
                        count++;
                        SPDLOG_INFO("[ConfigFactory] note_door[{}] = {} (vanilla: {})", i, val, sVanillaNoteDoors[i]);
                    }
                }

                if (count > 0) {
                    blob.patchU16(secPos, count);
                    sectionCount++;
                } else {
                    blob.data.resize(secPos - 2);
                }
            }

            // Section 8: JIGGY_PUZZLES
            {
                size_t secPos = blob.beginSection(ConfigSectionType::JIGGY_PUZZLES);
                uint16_t count = 0;

                for (int i = 0; i < 11; i++) {
                    uint8_t cost = fcf[FCF698_JIGGY_PUZZLES_OFF + i * 4];
                    if (cost != sVanillaJiggyCosts[i]) {
                        blob.writeU8(static_cast<uint8_t>(i));
                        blob.writeU8(cost);
                        count++;
                        SPDLOG_INFO("[ConfigFactory] jiggy_puzzle[{}] = {} (vanilla: {})", i, cost,
                                    sVanillaJiggyCosts[i]);
                    }
                }

                if (count > 0) {
                    blob.patchU16(secPos, count);
                    sectionCount++;
                } else {
                    blob.data.resize(secPos - 2);
                }
            }
        } else {
            SPDLOG_WARN("[ConfigFactory] FCF698 validation failed: first door value "
                        "{} out of range",
                        firstDoor);
        }
    }

    // Patch section count
    blob.patchU16(sectionCountPos, sectionCount);

    if (sectionCount == 0) {
        return {};
    }

    SPDLOG_INFO("[ConfigFactory] Built aGameConfig blob: {} bytes, {} sections", blob.data.size(), sectionCount);
    return blob.data;
}

// Asset-table hashing
//
// Lighthouse ships a per-slot SHA-1 of the v1.0 baseline at assets/yaml/<region>/<rev>/hashes.yaml.
// When extracting a BB romhack, we hash each slot's compressed bytes: a match means the slot is bit-for-bit
// vanilla, so skip the factory entirely.

// Walk the BK64 asset table at ROM 0x5E90 and invoke `onSlot(index, sha1Hex)` for every non-empty,
// non-zero-sized slot. Returns the total slot count (including empty slots), or 0 on parse failure.
static size_t WalkAssetTable(const std::vector<uint8_t>& rom,
                             const std::function<void(uint32_t, const std::string&)>& onSlot) {
    if (rom.size() < 0x6000) {
        SPDLOG_ERROR("ROM too small to contain BK64 asset table");
        return 0;
    }

    constexpr uint32_t kAssetTableOffset = 0x5E90;

    LUS::BinaryReader reader(reinterpret_cast<char*>(const_cast<uint8_t*>(rom.data() + kAssetTableOffset)),
                             rom.size() - kAssetTableOffset);
    reader.SetEndianness(Torch::Endianness::Big);
    const uint32_t assetCount = reader.ReadUInt32();
    reader.ReadUInt32();

    if (assetCount == 0 || assetCount > 0x10000) {
        SPDLOG_ERROR("Implausible asset count {} at 0x{:X}", assetCount, kAssetTableOffset);
        return 0;
    }

    const uint32_t dataStart = kAssetTableOffset + 8 + assetCount * 8;
    if (dataStart >= rom.size()) {
        SPDLOG_ERROR("Asset table extends past ROM end");
        return 0;
    }

    struct Entry {
        uint32_t blobOffset;
        int16_t compressionFlag;
        int16_t tFlag;
    };
    std::vector<Entry> entries(assetCount);
    for (uint32_t i = 0; i < assetCount; i++) {
        entries[i].blobOffset = reader.ReadUInt32();
        entries[i].compressionFlag = reader.ReadInt16();
        entries[i].tFlag = reader.ReadInt16();
    }

    for (uint32_t i = 0; i < assetCount; i++) {
        const auto& e = entries[i];
        if (e.tFlag == 4) {
            continue;
        }

        uint32_t endRel = 0;
        bool haveEnd = false;
        for (uint32_t j = i + 1; j < assetCount; j++) {
            if (entries[j].blobOffset != e.blobOffset) {
                endRel = entries[j].blobOffset;
                haveEnd = true;
                break;
            }
        }
        if (!haveEnd) {
            endRel = static_cast<uint32_t>(rom.size() - dataStart);
        }
        if (endRel <= e.blobOffset) {
            continue;
        }
        const uint32_t absOff = dataStart + e.blobOffset;
        const uint32_t size = endRel - e.blobOffset;
        if (absOff + size > rom.size()) {
            continue;
        }

        sha1::SHA1 s;
        s.processBytes(rom.data() + absOff, size);
        uint32_t digest[5];
        s.getDigest(digest);
        char buf[41];
        std::snprintf(buf, sizeof(buf), "%08x%08x%08x%08x%08x", digest[0], digest[1], digest[2], digest[3], digest[4]);

        onSlot(i, std::string(buf));
    }

    return assetCount;
}

void EmitAssetHashes(const std::filesystem::path& romPath, const std::filesystem::path& outputPath) {
    std::ifstream input(romPath, std::ios::binary);
    if (!input) {
        SPDLOG_ERROR("Failed to open ROM at {}", romPath.string());
        return;
    }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(input)), {});
    input.close();

    std::ofstream out(outputPath);
    if (!out) {
        SPDLOG_ERROR("Failed to open output {}", outputPath.string());
        return;
    }
    out << "# Auto-generated by `torch hashes`. Per-slot SHA-1 of the compressed\n"
        << "# byte range that the BK64 asset table at 0x5E90 points to. Used by\n"
        << "# Torch to skip factory dispatch for unmodified slots when extracting\n"
        << "# BB-style romhacks. DO NOT hand-edit.\n"
        << "hashes:\n";

    size_t hashed = 0;
    const size_t total = WalkAssetTable(rom, [&](uint32_t i, const std::string& sha1) {
        out << "  0x" << std::hex << std::uppercase << i << std::dec << ": " << sha1 << "\n";
        hashed++;
    });
    out.close();

    SPDLOG_INFO("Hashed {} of {} slots ({} empty/zero-sized) -> {}", hashed, total, total - hashed,
                outputPath.string());
}

size_t CountModifiedSlots(const std::vector<uint8_t>& rom, const std::filesystem::path& hashesYamlPath) {
    namespace fs = std::filesystem;
    if (!fs::exists(hashesYamlPath)) {
        SPDLOG_WARN("hashes.yaml not found at {}; cannot count modified slots", hashesYamlPath.string());
        return 0;
    }
    std::unordered_map<uint32_t, std::string> baseline;
    try {
        YAML::Node root = YAML::LoadFile(hashesYamlPath.string());
        YAML::Node hashes = root["hashes"];
        if (hashes && hashes.IsMap()) {
            for (auto it = hashes.begin(); it != hashes.end(); ++it) {
                baseline[it->first.as<uint32_t>()] = it->second.as<std::string>();
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to parse hashes.yaml: {}", e.what());
        return 0;
    }

    size_t modified = 0;
    WalkAssetTable(rom, [&](uint32_t i, const std::string& sha1) {
        auto it = baseline.find(i);
        if (it == baseline.end() || it->second != sha1) {
            modified++;
        }
    });
    return modified;
}

namespace {
struct BaselineHashCache {
    std::unordered_map<uint32_t, std::string> entries;
    bool attempted = false;
};
BaselineHashCache& GetBaselineCache() {
    static BaselineHashCache cache;
    return cache;
}
} // namespace

const std::string& GetBaselineAssetHash(uint32_t assetId) {
    namespace fs = std::filesystem;
    static const std::string kEmpty;

    auto& cache = GetBaselineCache();
    if (!cache.attempted) {
        cache.attempted = true;
        const auto path = fs::path(Companion::Instance->GetAssetPath()) / "hashes.yaml";
        if (fs::exists(path)) {
            try {
                YAML::Node root = YAML::LoadFile(path.string());
                YAML::Node hashes = root["hashes"];
                if (hashes && hashes.IsMap()) {
                    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
                        cache.entries[it->first.as<uint32_t>()] = it->second.as<std::string>();
                    }
                    SPDLOG_WARN("Loaded {} baseline asset hashes from {}", cache.entries.size(), path.string());
                }
            } catch (const std::exception& e) {
                SPDLOG_WARN("Failed to load baseline hashes at {}: {}", path.string(), e.what());
            }
        }
    }

    auto it = cache.entries.find(assetId);
    return (it != cache.entries.end()) ? it->second : kEmpty;
}

bool TrySynthesizeRomConfig(YAML::Node& config, const std::string& cartHash, const std::filesystem::path& romPath,
                            const std::vector<uint8_t>& rom) {
    if (config[cartHash]) {
        return false;
    }

    // Classification confirms the ROM actually carries a BK boot overlay. An extended
    // ROM that isn't BK-derived won't match the boot pattern, so we bail instead of
    // running a BK64 extraction that was never going to work.
    const RomhackKind kind = ClassifyRomhack(rom);
    if (kind == RomhackKind::NotRomhack) {
        return false;
    }
    if (kind == RomhackKind::UnknownRom) {
        SPDLOG_WARN("[ConfigFactory] {} is >16MB but doesn't look like a BK romhack. Aborting extraction.", cartHash);
        return false;
    }

    const char* buildType = kind == RomhackKind::BBRomhack ? "Banjo's Backpack" : "custom";

    std::string stem = romPath.stem().string();
    std::string slug;
    slug.reserve(stem.size());
    bool lastWasDash = true; // suppress leading dash
    for (char c : stem) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            slug += c;
            lastWasDash = false;
        } else if (c >= 'A' && c <= 'Z') {
            slug += static_cast<char>(c - 'A' + 'a');
            lastWasDash = false;
        } else if (!lastWasDash) {
            slug += '-';
            lastWasDash = true;
        }
    }
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    if (slug.empty())
        slug = cartHash.substr(0, 8);

    YAML::Node entry;
    entry["name"] = std::string(kind == RomhackKind::BBRomhack ? "BB Romhack" : "Custom BK Romhack") +
                    " (auto-detected: " + stem + ")";
    entry["path"] = "assets/yaml/us/rev0";
    YAML::Node entryCfg;
    entryCfg["gbi"] = "F3DEX_BK64";
    entryCfg["sort"] = "OFFSET";
    entryCfg["logging"] = "WARN";
    entryCfg["textures"] = "ADDITIONAL_DEFINES";
    entryCfg["include_autogen"] = true;
    YAML::Node entryOut;
    entryOut["binary"] = "mods/~romhacks/" + slug + ".o2r";
    entryOut["code"] = "src/assets";
    entryOut["headers"] = "include/assets";
    entryOut["modding"] = "src/assets";
    entryCfg["output"] = entryOut;
    entry["config"] = entryCfg;
    config[cartHash] = entry;

    SPDLOG_WARN("[ConfigFactory] Auto-detected BK romhack {} (build: {}); routing to mods/~romhacks/{}.o2r", cartHash,
                buildType, slug);
    return true;
}

} // namespace BK64
