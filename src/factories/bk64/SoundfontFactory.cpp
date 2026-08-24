#include "SoundfontFactory.h"
#include "BKByteUtils.h"
#include "VadpcmEncode.h"

#include <filesystem>
#include <optional>
#include <fstream>

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace BK64 {

namespace {

// AL_BANK_VERSION magic
constexpr uint16_t kAlBankRevision = 0x4231;

// Record layouts, all big-endian, all offsets relative to the ctl start:
//
//   ALBankFile:  [0] s16 revision, [2] s16 bankCount, [4+] u32 bankOffsets[]
//   ALBank:      [0] s16 instCount, [2] u8 flags, [3] u8 pad, [4] s32 sampleRate,
//                [8] u32 percussion, [12+] u32 instOffsets[]
//   ALInstrument:[0] u8 volume..vibDelay (12 bytes), [12] s16 bendRange,
//                [14] s16 soundCount, [16+] u32 soundOffsets[]
//   ALSound:     [0] u32 envelope, [4] u32 keyMap, [8] u32 wavetable,
//                [12] u8 samplePan, [13] u8 sampleVolume, [14] u8 flags
//   ALEnvelope:  [0] s32 attackTime, [4] s32 decayTime, [8] s32 releaseTime,
//                [12] u8 attackVolume, [13] u8 decayVolume
//   ALKeyMap:    [0] u8 velocityMin, [1] u8 velocityMax, [2] u8 keyMin,
//                [3] u8 keyMax, [4] u8 keyBase, [5] s8 detune
//   ALWaveTable: [0] u32 base, [4] s32 len, [8] u8 type, [9] u8 flags,
//                [12] u32 loop, [16] u32 book
//   ALADPCMBook: [0] s32 order, [4] s32 npredictors, [8+] s16 book[]
//   ALADPCMloop: [0] u32 start, [4] u32 end, [8] u32 count, [12] s16 state[16]

constexpr uint8_t kAdpcmWave = 0;

class CtlReader {
  public:
    CtlReader(const uint8_t* data, size_t size) : mData(data), mSize(size) {
    }

    void Require(uint32_t off, uint64_t need, const char* what) const {
        if (static_cast<uint64_t>(off) + need > mSize) {
            throw std::runtime_error(std::string("Soundfont: ctl walk ran past the end reading ") + what);
        }
    }

    uint8_t U8(uint32_t offset) const {
        return mData[offset];
    }
    int8_t S8(uint32_t offset) const {
        return static_cast<int8_t>(mData[offset]);
    }
    int16_t S16(uint32_t offset) const {
        return ReadS16BE(mData + offset);
    }
    uint32_t U32(uint32_t offset) const {
        return ReadU32BE(mData + offset);
    }
    int32_t S32(uint32_t offset) const {
        return ReadS32BE(mData + offset);
    }

  private:
    const uint8_t* mData;
    size_t mSize;
};

class ImageWriter {
  public:
    explicit ImageWriter(SoundfontImage& image) : mImage(image) {
    }

    void U8(uint32_t off, uint8_t value) {
        if (off >= mImage.bytes.size()) {
            throw std::runtime_error("Soundfont: serialized record runs past the end of the ctl");
        }
        mImage.bytes[off] = value;
        mImage.covered[off] = 1;
    }
    void S8(uint32_t off, int8_t value) {
        U8(off, static_cast<uint8_t>(value));
    }
    void U16(uint32_t off, uint16_t value) {
        U8(off, static_cast<uint8_t>(value >> 8));
        U8(off + 1, static_cast<uint8_t>(value));
    }
    void S16(uint32_t off, int16_t value) {
        U16(off, static_cast<uint16_t>(value));
    }
    void U32(uint32_t off, uint32_t value) {
        U16(off, static_cast<uint16_t>(value >> 16));
        U16(off + 2, static_cast<uint16_t>(value));
    }
    void S32(uint32_t off, int32_t value) {
        U32(off, static_cast<uint32_t>(value));
    }

  private:
    SoundfontImage& mImage;
};

void ParseBook(const CtlReader& reader, SoundfontData& font, uint32_t off) {
    if (off == 0 || font.mBooks.count(off) != 0) {
        return;
    }
    reader.Require(off, 8, "ALADPCMBook header");

    SoundfontBook book;
    book.order = reader.S32(off);
    book.npredictors = reader.S32(off + 4);
    if (book.order <= 0 || book.npredictors <= 0) {
        throw std::runtime_error("Soundfont: ALADPCMBook order/npredictors not positive");
    }

    const uint64_t count = static_cast<uint64_t>(book.order) * static_cast<uint64_t>(book.npredictors) * 8;
    reader.Require(off + 8, count * 2, "ALADPCMBook table");

    book.book.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; i++) {
        book.book.push_back(reader.S16(off + 8 + static_cast<uint32_t>(i * 2)));
    }
    font.mBooks.emplace(off, std::move(book));
}

void ParseLoop(const CtlReader& reader, SoundfontData& font, uint32_t off) {
    if (off == 0 || font.mLoops.count(off) != 0) {
        return;
    }
    reader.Require(off, 44, "ALADPCMloop");

    SoundfontLoop loop;
    loop.start = reader.U32(off);
    loop.end = reader.U32(off + 4);
    loop.count = reader.U32(off + 8);
    loop.state.reserve(16);
    for (uint32_t i = 0; i < 16; i++) {
        loop.state.push_back(reader.S16(off + 12 + i * 2));
    }
    font.mLoops.emplace(off, std::move(loop));
}

void ParseWave(const CtlReader& reader, SoundfontData& font, uint32_t off) {
    if (off == 0 || font.mWaves.count(off) != 0) {
        return;
    }
    reader.Require(off, 12, "ALWaveTable header");

    SoundfontWave wave;
    wave.base = reader.U32(off);
    wave.len = reader.S32(off + 4);
    wave.type = reader.U8(off + 8);
    wave.flags = reader.U8(off + 9);
    if (wave.len < 0) {
        throw std::runtime_error("Soundfont: negative wavetable len");
    }

    if (wave.type != kAdpcmWave) {
        throw std::runtime_error("Soundfont: only ADPCM wavetables are supported");
    }
    reader.Require(off + 12, 8, "ALWaveTable adpcmWave");
    wave.loopOffset = reader.U32(off + 12);
    wave.bookOffset = reader.U32(off + 16);

    // Insert before recursing so a self-referential offset cannot loop forever.
    const uint32_t loopOffset = wave.loopOffset;
    const uint32_t bookOffset = wave.bookOffset;
    font.mWaves.emplace(off, std::move(wave));

    ParseLoop(reader, font, loopOffset);
    ParseBook(reader, font, bookOffset);
}

void ParseEnvelope(const CtlReader& reader, SoundfontData& font, uint32_t off) {
    if (off == 0 || font.mEnvelopes.count(off) != 0) {
        return;
    }
    reader.Require(off, 14, "ALEnvelope");

    SoundfontEnvelope env;
    env.attackTime = reader.S32(off);
    env.decayTime = reader.S32(off + 4);
    env.releaseTime = reader.S32(off + 8);
    env.attackVolume = reader.U8(off + 12);
    env.decayVolume = reader.U8(off + 13);
    font.mEnvelopes.emplace(off, env);
}

void ParseKeyMap(const CtlReader& reader, SoundfontData& font, uint32_t off) {
    if (off == 0 || font.mKeyMaps.count(off) != 0) {
        return;
    }
    reader.Require(off, 6, "ALKeyMap");

    SoundfontKeyMap keymap;
    keymap.velocityMin = reader.U8(off);
    keymap.velocityMax = reader.U8(off + 1);
    keymap.keyMin = reader.U8(off + 2);
    keymap.keyMax = reader.U8(off + 3);
    keymap.keyBase = reader.U8(off + 4);
    keymap.detune = reader.S8(off + 5);
    font.mKeyMaps.emplace(off, keymap);
}

void ParseSound(const CtlReader& reader, SoundfontData& font, uint32_t off) {
    if (off == 0 || font.mSounds.count(off) != 0) {
        return;
    }
    reader.Require(off, 15, "ALSound");

    SoundfontSound sound;
    sound.envelopeOffset = reader.U32(off);
    sound.keyMapOffset = reader.U32(off + 4);
    sound.waveOffset = reader.U32(off + 8);
    sound.samplePan = reader.U8(off + 12);
    sound.sampleVolume = reader.U8(off + 13);
    sound.flags = reader.U8(off + 14);
    font.mSounds.emplace(off, sound);

    ParseEnvelope(reader, font, sound.envelopeOffset);
    ParseKeyMap(reader, font, sound.keyMapOffset);
    ParseWave(reader, font, sound.waveOffset);
}

void ParseInstrument(const CtlReader& reader, SoundfontData& font, uint32_t off) {
    if (off == 0 || font.mInstruments.count(off) != 0) {
        return;
    }
    reader.Require(off, 16, "ALInstrument header");

    const int16_t soundCount = reader.S16(off + 14);
    if (soundCount < 0) {
        throw std::runtime_error("Soundfont: negative instrument soundCount");
    }
    reader.Require(off + 16, static_cast<uint64_t>(soundCount) * 4, "ALInstrument soundArray");

    SoundfontInstrument inst;
    inst.volume = reader.U8(off);
    inst.pan = reader.U8(off + 1);
    inst.priority = reader.U8(off + 2);
    inst.flags = reader.U8(off + 3);
    inst.tremType = reader.U8(off + 4);
    inst.tremRate = reader.U8(off + 5);
    inst.tremDepth = reader.U8(off + 6);
    inst.tremDelay = reader.U8(off + 7);
    inst.vibType = reader.U8(off + 8);
    inst.vibRate = reader.U8(off + 9);
    inst.vibDepth = reader.U8(off + 10);
    inst.vibDelay = reader.U8(off + 11);
    inst.bendRange = reader.S16(off + 12);
    inst.soundOffsets.reserve(static_cast<size_t>(soundCount));
    for (int16_t i = 0; i < soundCount; i++) {
        inst.soundOffsets.push_back(reader.U32(off + 16 + static_cast<uint32_t>(i) * 4));
    }

    const std::vector<uint32_t> sounds = inst.soundOffsets;
    font.mInstruments.emplace(off, std::move(inst));

    for (uint32_t sndOff : sounds) {
        ParseSound(reader, font, sndOff);
    }
}

void ParseBank(const CtlReader& reader, SoundfontData& font, uint32_t off) {
    if (off == 0 || font.mBanks.count(off) != 0) {
        return;
    }
    reader.Require(off, 12, "ALBank header");

    const int16_t instCount = reader.S16(off);
    if (instCount < 0) {
        throw std::runtime_error("Soundfont: negative bank instCount");
    }
    reader.Require(off + 12, static_cast<uint64_t>(instCount) * 4, "ALBank instArray");

    SoundfontBank bank;
    bank.flags = reader.U8(off + 2);
    bank.pad = reader.U8(off + 3);
    bank.sampleRate = reader.S32(off + 4);
    bank.percussionOffset = reader.U32(off + 8);
    bank.instrumentOffsets.reserve(static_cast<size_t>(instCount));
    for (int16_t i = 0; i < instCount; i++) {
        bank.instrumentOffsets.push_back(reader.U32(off + 12 + static_cast<uint32_t>(i) * 4));
    }

    // Neither bank has one, so there is no way to test a path that handled it.
    if (bank.percussionOffset != 0) {
        throw std::runtime_error("Soundfont: percussion instruments are not supported");
    }

    const std::vector<uint32_t> instruments = bank.instrumentOffsets;
    font.mBanks.emplace(off, std::move(bank));

    for (uint32_t instOff : instruments) {
        ParseInstrument(reader, font, instOff);
    }
}

bool ValidateCtl(const uint8_t* ctl, size_t ctlSize) {
    if (ctlSize < 8) {
        return false;
    }
    if (ReadU16BE(ctl) != kAlBankRevision) {
        return false;
    }
    int16_t bankCount = ReadS16BE(ctl + 2);
    if (bankCount <= 0 || bankCount > 16) {
        return false;
    }
    uint32_t bank0 = ReadU32BE(ctl + 4);
    if (bank0 != 0 && (bank0 < 4u + 4u * bankCount || bank0 >= ctlSize)) {
        return false;
    }
    try {
        return ParseSoundfont(ctl, ctlSize).SampleDataEnd() != 0;
    } catch (...) { return false; }
}

void VerifySoundfont(const SoundfontData& font, uint32_t ctlOffset) {
    SoundfontImage image;
    try {
        image = font.Serialize();
    } catch (const std::exception& envelope) {
        SPDLOG_ERROR("Soundfont ctl@0x{:X}: could not rebuild for verification: {}", ctlOffset, envelope.what());
        return;
    }

    size_t mismatches = 0;
    size_t firstMismatch = 0;
    size_t unclaimed = 0;
    size_t unclaimedNonZero = 0;
    size_t firstNonZero = 0;

    for (size_t i = 0; i < font.mBuffer.size(); i++) {
        if (image.covered[i]) {
            if (image.bytes[i] != font.mBuffer[i]) {
                if (mismatches++ == 0) {
                    firstMismatch = i;
                }
            }
        } else {
            unclaimed++;
            if (font.mBuffer[i] != 0 && unclaimedNonZero++ == 0) {
                firstNonZero = i;
            }
        }
    }

    SPDLOG_INFO("Soundfont ctl@0x{:X}: {} bank(s), {} instruments, {} sounds, {} wavetables, {} books, {} loops, "
                "{} envelopes, {} keymaps; sample data ends at 0x{:X}",
                ctlOffset, font.mBankOffsets.size(), font.mInstruments.size(), font.mSounds.size(), font.mWaves.size(),
                font.mBooks.size(), font.mLoops.size(), font.mEnvelopes.size(), font.mKeyMaps.size(),
                font.SampleDataEnd());

    if (mismatches != 0) {
        SPDLOG_ERROR("Soundfont ctl@0x{:X}: rebuild differs from the ROM in {} byte(s), first at 0x{:X}", ctlOffset,
                     mismatches, firstMismatch);
    }
    if (unclaimedNonZero != 0) {
        SPDLOG_ERROR("Soundfont ctl@0x{:X}: {} non-zero byte(s) belong to no record, first at 0x{:X}", ctlOffset,
                     unclaimedNonZero, firstNonZero);
    }
    if (mismatches == 0 && unclaimedNonZero == 0) {
        SPDLOG_DEBUG("Soundfont ctl@0x{:X}: round-trip clean ({} padding bytes)", ctlOffset, unclaimed);
    }
}

} // namespace

SoundfontData ParseSoundfont(const uint8_t* ctl, size_t ctlSize) {
    const CtlReader reader(ctl, ctlSize);
    SoundfontData font(std::vector<uint8_t>(ctl, ctl + ctlSize));

    reader.Require(0, 4, "ALBankFile header");
    font.mRevision = reader.S16(0);

    const int16_t bankCount = reader.S16(2);
    if (bankCount <= 0) {
        throw std::runtime_error("Soundfont: bankCount <= 0");
    }
    reader.Require(4, static_cast<uint64_t>(bankCount) * 4, "ALBankFile bankArray");

    font.mBankOffsets.reserve(static_cast<size_t>(bankCount));
    for (int16_t i = 0; i < bankCount; i++) {
        font.mBankOffsets.push_back(reader.U32(4 + static_cast<uint32_t>(i) * 4));
    }
    for (uint32_t bankOff : font.mBankOffsets) {
        ParseBank(reader, font, bankOff);
    }

    return font;
}

uint64_t SoundfontData::SampleDataEnd() const {
    uint64_t end = 0;
    for (const auto& [off, wave] : mWaves) {
        const uint64_t waveEnd = static_cast<uint64_t>(wave.base) + static_cast<uint64_t>(wave.len);
        if (waveEnd > end) {
            end = waveEnd;
        }
    }
    return end;
}

std::vector<uint8_t> SoundfontData::SampleBytes(const SoundfontWave& wave) const {
    const uint64_t end = static_cast<uint64_t>(wave.base) + static_cast<uint64_t>(wave.len);
    if (wave.len <= 0 || end > mSampleData.size()) {
        return {};
    }
    return std::vector<uint8_t>(mSampleData.begin() + wave.base, mSampleData.begin() + static_cast<size_t>(end));
}

SoundfontImage SoundfontData::Serialize() const {
    SoundfontImage image;
    image.bytes.assign(mBuffer.size(), 0);
    image.covered.assign(mBuffer.size(), 0);
    ImageWriter writer(image);

    writer.S16(0, mRevision);
    writer.S16(2, static_cast<int16_t>(mBankOffsets.size()));
    for (size_t i = 0; i < mBankOffsets.size(); i++) {
        writer.U32(static_cast<uint32_t>(4 + i * 4), mBankOffsets[i]);
    }

    for (const auto& [off, bank] : mBanks) {
        writer.S16(off, static_cast<int16_t>(bank.instrumentOffsets.size()));
        writer.U8(off + 2, bank.flags);
        writer.U8(off + 3, bank.pad);
        writer.S32(off + 4, bank.sampleRate);
        writer.U32(off + 8, bank.percussionOffset);
        for (size_t i = 0; i < bank.instrumentOffsets.size(); i++) {
            writer.U32(off + 12 + static_cast<uint32_t>(i * 4), bank.instrumentOffsets[i]);
        }
    }

    for (const auto& [off, inst] : mInstruments) {
        writer.U8(off, inst.volume);
        writer.U8(off + 1, inst.pan);
        writer.U8(off + 2, inst.priority);
        writer.U8(off + 3, inst.flags);
        writer.U8(off + 4, inst.tremType);
        writer.U8(off + 5, inst.tremRate);
        writer.U8(off + 6, inst.tremDepth);
        writer.U8(off + 7, inst.tremDelay);
        writer.U8(off + 8, inst.vibType);
        writer.U8(off + 9, inst.vibRate);
        writer.U8(off + 10, inst.vibDepth);
        writer.U8(off + 11, inst.vibDelay);
        writer.S16(off + 12, inst.bendRange);
        writer.S16(off + 14, static_cast<int16_t>(inst.soundOffsets.size()));
        for (size_t i = 0; i < inst.soundOffsets.size(); i++) {
            writer.U32(off + 16 + static_cast<uint32_t>(i * 4), inst.soundOffsets[i]);
        }
    }

    for (const auto& [off, sound] : mSounds) {
        writer.U32(off, sound.envelopeOffset);
        writer.U32(off + 4, sound.keyMapOffset);
        writer.U32(off + 8, sound.waveOffset);
        writer.U8(off + 12, sound.samplePan);
        writer.U8(off + 13, sound.sampleVolume);
        writer.U8(off + 14, sound.flags);
    }

    for (const auto& [off, env] : mEnvelopes) {
        writer.S32(off, env.attackTime);
        writer.S32(off + 4, env.decayTime);
        writer.S32(off + 8, env.releaseTime);
        writer.U8(off + 12, env.attackVolume);
        writer.U8(off + 13, env.decayVolume);
    }

    for (const auto& [off, keymap] : mKeyMaps) {
        writer.U8(off, keymap.velocityMin);
        writer.U8(off + 1, keymap.velocityMax);
        writer.U8(off + 2, keymap.keyMin);
        writer.U8(off + 3, keymap.keyMax);
        writer.U8(off + 4, keymap.keyBase);
        writer.S8(off + 5, keymap.detune);
    }

    for (const auto& [off, wave] : mWaves) {
        writer.U32(off, wave.base);
        writer.S32(off + 4, wave.len);
        writer.U8(off + 8, wave.type);
        writer.U8(off + 9, wave.flags);
        writer.U32(off + 12, wave.loopOffset);
        if (wave.type == kAdpcmWave) {
            writer.U32(off + 16, wave.bookOffset);
        }
    }

    for (const auto& [off, loop] : mLoops) {
        writer.U32(off, loop.start);
        writer.U32(off + 4, loop.end);
        writer.U32(off + 8, loop.count);
        for (size_t i = 0; i < loop.state.size(); i++) {
            writer.S16(off + 12 + static_cast<uint32_t>(i * 2), loop.state[i]);
        }
    }

    for (const auto& [off, book] : mBooks) {
        writer.S32(off, book.order);
        writer.S32(off + 4, book.npredictors);
        for (size_t i = 0; i < book.book.size(); i++) {
            writer.S16(off + 8 + static_cast<uint32_t>(i * 2), book.book[i]);
        }
    }

    return image;
}

uint32_t LocateSoundfontCtl(const std::vector<uint8_t>& rom, uint32_t ctlOffset, uint32_t ctlSize) {
    if ((size_t)ctlOffset + ctlSize <= rom.size() && ValidateCtl(rom.data() + ctlOffset, ctlSize)) {
        return ctlOffset;
    }

    // Romhacks shift the whole audio region, so hunt for the real header.
    std::vector<uint32_t> candidates;
    for (size_t i = 0; i + 8 <= rom.size(); i += 8) {
        if (rom[i] != 0x42 || rom[i + 1] != 0x31) {
            continue;
        }
        size_t avail = std::min<size_t>(ctlSize, rom.size() - i);
        if (ValidateCtl(rom.data() + i, avail)) {
            candidates.push_back((uint32_t)i);
        }
    }

    if (candidates.empty()) {
        SPDLOG_ERROR("SoundfontCtl: no valid ALBankFile found anywhere in ROM (vanilla ctl@0x{:X} size=0x{:X})",
                     ctlOffset, ctlSize);
        throw std::runtime_error("SoundfontCtl: soundfont ctl not found in ROM");
    }

    uint32_t best = candidates[0];
    for (uint32_t c : candidates) {
        auto dist = [&](uint32_t off) { return off > ctlOffset ? off - ctlOffset : ctlOffset - off; };
        if (dist(c) < dist(best)) {
            best = c;
        }
    }

    SPDLOG_WARN("SoundfontCtl: ctl not at vanilla offset 0x{:X}; relocated to 0x{:X} (delta 0x{:X}, {} candidates)",
                ctlOffset, best, (uint32_t)(best - ctlOffset), candidates.size());
    return best;
}

std::optional<std::shared_ptr<IParsedData>> SoundfontFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto ctlOffset = GetSafeNode<uint32_t>(node, "offset");
    const auto ctlSize = GetSafeNode<uint32_t>(node, "size");
    const auto tblOffset = GetSafeNode<uint32_t>(node, "tbl_offset");

    // The tbl follows the ctl, so it shifts by the same delta.
    const uint32_t realCtlOffset = LocateSoundfontCtl(buffer, ctlOffset, ctlSize);
    const uint32_t realTblOffset = tblOffset + (realCtlOffset - ctlOffset);

    if ((size_t)realCtlOffset + ctlSize > buffer.size()) {
        throw std::runtime_error("SoundfontFactory: ctl exceeds ROM size");
    }

    auto soundfont = std::make_shared<SoundfontData>(ParseSoundfont(buffer.data() + realCtlOffset, ctlSize));

    // No size of its own: it runs to the end of the sample data the wavetables
    // point at, rounded to BK's 16-byte alignment.
    const uint64_t sampleEnd = soundfont->SampleDataEnd();
    if (sampleEnd == 0) {
        throw std::runtime_error("SoundfontFactory: ctl referenced no wavetables");
    }
    const size_t tblSize = static_cast<size_t>((sampleEnd + 0xF) & ~static_cast<uint64_t>(0xF));
    if ((size_t)realTblOffset + tblSize > buffer.size()) {
        throw std::runtime_error("SoundfontFactory: computed tbl size exceeds ROM bounds");
    }
    soundfont->mSampleData.assign(buffer.begin() + realTblOffset, buffer.begin() + realTblOffset + tblSize);

    VerifySoundfont(*soundfont, realCtlOffset);
    SPDLOG_INFO("Soundfont ctl@0x{:X}: tbl@0x{:X} size 0x{:X}", realCtlOffset, realTblOffset, tblSize);

    return soundfont;
}

namespace {

struct SoundNaming {
    std::string sfxPath = "sfx";
    std::string instPath = "music";
    int64_t sfxBase = -1;
    std::map<uint32_t, std::string> names;
    std::map<uint32_t, uint32_t> users;
    std::map<uint32_t, std::string> instNames;
};

SoundNaming ReadNaming(YAML::Node& node) {
    SoundNaming naming;
    naming.sfxPath = GetSafeNode<std::string>(node, "sfx_path", naming.sfxPath);
    naming.instPath = GetSafeNode<std::string>(node, "inst_path", naming.instPath);
    if (node["sfx_base"]) {
        naming.sfxBase = node["sfx_base"].as<int64_t>();
    }

    const YAML::Node& config = Companion::Instance->GetCurrentFileConfig();
    if (config && config["sfx_names"]) {
        for (auto it = config["sfx_names"].begin(); it != config["sfx_names"].end(); ++it) {
            naming.names[it->first.as<uint32_t>()] = it->second.as<std::string>();
        }
    }
    if (config && config["instrument_names"]) {
        for (auto it = config["instrument_names"].begin(); it != config["instrument_names"].end(); ++it) {
            naming.instNames[it->first.as<uint32_t>()] = it->second.as<std::string>();
        }
    }
    if (config && config["sfx_users"]) {
        for (auto it = config["sfx_users"].begin(); it != config["sfx_users"].end(); ++it) {
            naming.users[it->first.as<uint32_t>()] = it->second.as<uint32_t>();
        }
    }
    return naming;
}

std::string Hex(uint64_t value, int width) {
    std::stringstream stream;
    stream << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
    return stream.str();
}

// Instruments have no names anywhere in the game, so the number a composer types
// is the name: decimal, matching the program change. A multisampled instrument
// splits by key range, and no two splits of one program share a range, so the
// range names the file and says which notes it covers.
std::string PathForSound(const SoundNaming& naming, const SoundfontData& font, size_t instIndex, size_t soundIndex,
                         size_t soundCount, uint32_t soundOffset) {
    if (instIndex == 0 && naming.sfxBase >= 0) {
        const uint32_t id = static_cast<uint32_t>(naming.sfxBase) + static_cast<uint32_t>(soundIndex);
        const auto it = naming.names.find(id);
        return naming.sfxPath + "/" + Hex(id, 3) + "_" + (it != naming.names.end() ? it->second : "UNNAMED");
    }

    std::string path = naming.instPath + "/program" + std::to_string(instIndex);
    const auto named = naming.instNames.find(static_cast<uint32_t>(instIndex));
    if (named != naming.instNames.end() && !named->second.empty()) {
        path += "_" + named->second;
    }
    if (soundCount <= 1) {
        return path;
    }
    const auto sound = font.mSounds.find(soundOffset);
    if (sound != font.mSounds.end()) {
        const auto keymap = font.mKeyMaps.find(sound->second.keyMapOffset);
        if (keymap != font.mKeyMaps.end()) {
            return path + "_keys" + std::to_string(static_cast<int>(keymap->second.keyMin)) + "-" +
                   std::to_string(static_cast<int>(keymap->second.keyMax));
        }
    }
    return path + "_" + std::to_string(soundIndex);
}

void WriteSoundResource(const SoundfontData& font, const SoundfontSound& sound, const std::string& path,
                        uint32_t userCount) {
    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::BKSound, 0);

    writer.Write(sound.samplePan);
    writer.Write(sound.sampleVolume);
    writer.Write(sound.flags);

    const auto env = font.mEnvelopes.find(sound.envelopeOffset);
    const bool hasEnv = env != font.mEnvelopes.end();
    writer.Write(static_cast<uint8_t>(hasEnv ? 1 : 0));
    if (hasEnv) {
        writer.Write(env->second.attackTime);
        writer.Write(env->second.decayTime);
        writer.Write(env->second.releaseTime);
        writer.Write(env->second.attackVolume);
        writer.Write(env->second.decayVolume);
    }

    const auto keymap = font.mKeyMaps.find(sound.keyMapOffset);
    const bool hasKm = keymap != font.mKeyMaps.end();
    writer.Write(static_cast<uint8_t>(hasKm ? 1 : 0));
    if (hasKm) {
        writer.Write(keymap->second.velocityMin);
        writer.Write(keymap->second.velocityMax);
        writer.Write(keymap->second.keyMin);
        writer.Write(keymap->second.keyMax);
        writer.Write(keymap->second.keyBase);
        writer.Write(static_cast<uint8_t>(keymap->second.detune));
    }

    const auto wave = font.mWaves.find(sound.waveOffset);
    const bool hasWave = wave != font.mWaves.end();
    writer.Write(static_cast<uint8_t>(hasWave ? 1 : 0));
    if (hasWave) {
        writer.Write(wave->second.type);
        writer.Write(wave->second.flags);

        const auto loop = font.mLoops.find(wave->second.loopOffset);
        const bool hasLoop = wave->second.loopOffset != 0 && loop != font.mLoops.end();
        writer.Write(static_cast<uint8_t>(hasLoop ? 1 : 0));
        if (hasLoop) {
            writer.Write(loop->second.start);
            writer.Write(loop->second.end);
            writer.Write(loop->second.count);
            writer.Write(static_cast<uint32_t>(loop->second.state.size()));
            for (int16_t value : loop->second.state) {
                writer.Write(value);
            }
        }

        const auto book = font.mBooks.find(wave->second.bookOffset);
        const bool hasBook = wave->second.bookOffset != 0 && book != font.mBooks.end();
        writer.Write(static_cast<uint8_t>(hasBook ? 1 : 0));
        if (hasBook) {
            writer.Write(book->second.order);
            writer.Write(book->second.npredictors);
            writer.Write(static_cast<uint32_t>(book->second.book.size()));
            for (int16_t value : book->second.book) {
                writer.Write(value);
            }
        }

        const std::vector<uint8_t> bytes = font.SampleBytes(wave->second);
        writer.Write(static_cast<uint32_t>(bytes.size()));
        writer.Write(reinterpret_cast<char*>(const_cast<uint8_t*>(bytes.data())), bytes.size());
    }

    // Appended last so anything reading only the sound itself stays valid.
    writer.Write(userCount);

    std::stringstream stream;
    writer.Finish(stream);
    const std::string blob = stream.str();
    Companion::Instance->RegisterCompanionFile(path, std::vector<char>(blob.begin(), blob.end()));
}

void WriteInstrument(LUS::BinaryWriter& writer, const SoundfontInstrument& inst,
                     const std::vector<std::string>& paths) {
    writer.Write(inst.volume);
    writer.Write(inst.pan);
    writer.Write(inst.priority);
    writer.Write(inst.flags);
    writer.Write(inst.tremType);
    writer.Write(inst.tremRate);
    writer.Write(inst.tremDepth);
    writer.Write(inst.tremDelay);
    writer.Write(inst.vibType);
    writer.Write(inst.vibRate);
    writer.Write(inst.vibDepth);
    writer.Write(inst.vibDelay);
    writer.Write(inst.bendRange);
    writer.Write(static_cast<uint32_t>(paths.size()));
    for (const std::string& p : paths) {
        writer.Write(p);
    }
}

} // namespace

ExportResult SoundfontBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto font = std::static_pointer_cast<SoundfontData>(raw);
    const SoundNaming naming = ReadNaming(node);
    const std::string root = Companion::Instance->GetCurrentDirectory();

    std::map<uint32_t, std::string> pathBySound;
    size_t written = 0;

    auto collect = [&](const SoundfontInstrument& inst, size_t instIndex) {
        std::vector<std::string> paths;
        paths.reserve(inst.soundOffsets.size());
        for (size_t k = 0; k < inst.soundOffsets.size(); k++) {
            const uint32_t soundOffset = inst.soundOffsets[k];
            if (soundOffset == 0) {
                paths.emplace_back();
                continue;
            }
            auto it = pathBySound.find(soundOffset);
            if (it == pathBySound.end()) {
                const std::string rel =
                    PathForSound(naming, *font, instIndex, k, inst.soundOffsets.size(), soundOffset);
                const auto sound = font->mSounds.find(soundOffset);
                if (sound == font->mSounds.end()) {
                    paths.emplace_back();
                    continue;
                }
                uint32_t userCount = 0;
                if (instIndex == 0 && naming.sfxBase >= 0) {
                    const auto found =
                        naming.users.find(static_cast<uint32_t>(naming.sfxBase) + static_cast<uint32_t>(k));
                    if (found != naming.users.end()) {
                        userCount = found->second;
                    }
                }
                WriteSoundResource(*font, sound->second, rel, userCount);
                written++;
                it = pathBySound.emplace(soundOffset, root.empty() ? rel : root + "/" + rel).first;
            }
            paths.push_back(it->second);
        }
        return paths;
    };

    LUS::BinaryWriter writer;
    WriteHeader(writer, Torch::ResourceType::BKSoundBank, 0);
    writer.Write(static_cast<uint32_t>(font->mBankOffsets.size()));

    for (uint32_t bankOffset : font->mBankOffsets) {
        const auto bank = font->mBanks.find(bankOffset);
        if (bank == font->mBanks.end()) {
            writer.Write(static_cast<int32_t>(0)); // sampleRate
            writer.Write(static_cast<uint8_t>(0)); // flags
            writer.Write(static_cast<uint8_t>(0)); // pad
            writer.Write(static_cast<uint32_t>(0));
            continue;
        }

        writer.Write(bank->second.sampleRate);
        writer.Write(bank->second.flags);
        writer.Write(bank->second.pad);

        writer.Write(static_cast<uint32_t>(bank->second.instrumentOffsets.size()));
        for (size_t i = 0; i < bank->second.instrumentOffsets.size(); i++) {
            const auto inst = font->mInstruments.find(bank->second.instrumentOffsets[i]);
            const bool present = bank->second.instrumentOffsets[i] != 0 && inst != font->mInstruments.end();
            writer.Write(static_cast<uint8_t>(present ? 1 : 0));
            if (present) {
                WriteInstrument(writer, inst->second, collect(inst->second, i));
            }
        }
    }

    SPDLOG_INFO("Soundfont '{}': wrote {} sound resources under '{}'", entryName, written, root);

    writer.Finish(write);
    return std::nullopt;
}

namespace {

int16_t Clamp16(int32_t value) {
    return static_cast<int16_t>(value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
}

std::vector<int16_t> DecodeAdpcm(const std::vector<uint8_t>& in, const SoundfontBook& book) {
    const size_t frames = in.size() / 9;
    std::vector<int16_t> out(frames * 16 + 16, 0);
    int16_t* dst = out.data() + 16;
    size_t at = 0;

    for (size_t f = 0; f < frames; f++) {
        const int shift = in[at] >> 4;
        int predictor = in[at++] & 0xF;
        if (predictor >= book.npredictors) {
            predictor = 0;
        }
        const int16_t* tbl0 = book.book.data() + predictor * 16;
        const int16_t* tbl1 = tbl0 + 8;

        for (int half = 0; half < 2; half++) {
            int16_t ins[8];
            const int16_t prev1 = dst[-1];
            const int16_t prev2 = dst[-2];
            for (int j = 0; j < 4; j++) {
                ins[j * 2] = static_cast<int16_t>((((in[at] >> 4) << 28) >> 28) << shift);
                ins[j * 2 + 1] = static_cast<int16_t>((((in[at++] & 0xF) << 28) >> 28) << shift);
            }
            for (int j = 0; j < 8; j++) {
                int32_t acc = tbl0[j] * prev2 + tbl1[j] * prev1 + (ins[j] << 11);
                for (int k = 0; k < j; k++) {
                    acc += tbl1[(j - k) - 1] * ins[k];
                }
                *dst++ = Clamp16(acc >> 11);
            }
        }
    }
    out.erase(out.begin(), out.begin() + 16);
    return out;
}

void PushLE(std::vector<uint8_t>& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; i++) {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

std::vector<uint8_t> BuildWav(const std::vector<int16_t>& pcm, int rate) {
    std::vector<uint8_t> out;
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
    const char* riff = "RIFF";
    out.insert(out.end(), riff, riff + 4);
    PushLE(out, 36 + dataBytes, 4);
    const char* wave = "WAVEfmt ";
    out.insert(out.end(), wave, wave + 8);
    PushLE(out, 16, 4);
    PushLE(out, 1, 2); // PCM
    PushLE(out, 1, 2); // mono
    PushLE(out, static_cast<uint32_t>(rate), 4);
    PushLE(out, static_cast<uint32_t>(rate) * 2, 4);
    PushLE(out, 2, 2);  // block align
    PushLE(out, 16, 2); // bits
    const char* data = "data";
    out.insert(out.end(), data, data + 4);
    PushLE(out, dataBytes, 4);
    for (int16_t sample : pcm) {
        PushLE(out, static_cast<uint16_t>(sample), 2);
    }
    return out;
}

std::vector<uint8_t> BuildRaw(const SoundfontBook* book, const SoundfontLoop* loop, const std::vector<uint8_t>& adpcm) {
    std::vector<uint8_t> out;
    PushLE(out, book ? static_cast<uint32_t>(book->order) : 0, 4);
    PushLE(out, book ? static_cast<uint32_t>(book->npredictors) : 0, 4);
    PushLE(out, book ? static_cast<uint32_t>(book->book.size()) : 0, 4);
    if (book) {
        for (int16_t value : book->book) {
            PushLE(out, static_cast<uint16_t>(value), 2);
        }
    }
    PushLE(out, loop ? static_cast<uint32_t>(loop->state.size()) : 0, 4);
    if (loop) {
        for (int16_t value : loop->state) {
            PushLE(out, static_cast<uint16_t>(value), 2);
        }
    }
    PushLE(out, static_cast<uint32_t>(adpcm.size()), 4);
    out.insert(out.end(), adpcm.begin(), adpcm.end());
    return out;
}

int PlaybackRate(int bankRate, const SoundfontKeyMap* keymap) {
    if (keymap == nullptr) {
        return bankRate;
    }
    const double cents = static_cast<double>(keymap->keyBase) * 100.0 + static_cast<double>(keymap->detune) - 6000.0;
    const double rate = static_cast<double>(bankRate) * std::pow(2.0, cents / 1200.0);
    return static_cast<int>(std::lround(rate));
}

void WriteSoundModFiles(const SoundfontData& font, const SoundfontSound& sound, const std::string& rel) {
    const auto env = font.mEnvelopes.find(sound.envelopeOffset);
    const auto keymap = font.mKeyMaps.find(sound.keyMapOffset);
    const auto wave = font.mWaves.find(sound.waveOffset);
    const SoundfontBook* book = nullptr;
    const SoundfontLoop* loop = nullptr;
    std::vector<uint8_t> adpcm;
    if (wave != font.mWaves.end()) {
        const auto b = font.mBooks.find(wave->second.bookOffset);
        const auto l = font.mLoops.find(wave->second.loopOffset);
        book = b != font.mBooks.end() ? &b->second : nullptr;
        loop = l != font.mLoops.end() ? &l->second : nullptr;
        adpcm = font.SampleBytes(wave->second);
    }

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Sample" << YAML::Value << "raw";
    out << YAML::Key << "SamplePan" << YAML::Value << (int)sound.samplePan;
    out << YAML::Key << "SampleVolume" << YAML::Value << (int)sound.sampleVolume;
    out << YAML::Key << "Flags" << YAML::Value << (int)sound.flags;
    if (env != font.mEnvelopes.end()) {
        out << YAML::Key << "Envelope" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "AttackTime" << YAML::Value << env->second.attackTime;
        out << YAML::Key << "AttackVolume" << YAML::Value << (int)env->second.attackVolume;
        out << YAML::Key << "DecayTime" << YAML::Value << env->second.decayTime;
        out << YAML::Key << "DecayVolume" << YAML::Value << (int)env->second.decayVolume;
        out << YAML::Key << "ReleaseTime" << YAML::Value << env->second.releaseTime;
        out << YAML::EndMap;
    }
    if (keymap != font.mKeyMaps.end()) {
        out << YAML::Key << "KeyBase" << YAML::Value << (int)keymap->second.keyBase;
        out << YAML::Key << "Detune" << YAML::Value << (int)keymap->second.detune;
        out << YAML::Key << "ChainNext" << YAML::Value
            << (int)(keymap->second.velocityMin + ((keymap->second.keyMin & 0xC0) * 4));
        out << YAML::Key << "ChainDelayFrames" << YAML::Value << (int)keymap->second.velocityMax;
        out << YAML::Key << "VolumeGroup" << YAML::Value << (int)(keymap->second.keyMin & 0x3F);
        out << YAML::Key << "ReverbSend" << YAML::Value << (int)(keymap->second.keyMax & 0x0F);
        out << YAML::Key << "KeyMaxFlags" << YAML::Value << (int)(keymap->second.keyMax & 0xF0);
    }
    if (loop != nullptr) {
        out << YAML::Key << "Loop" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Start" << YAML::Value << loop->start;
        out << YAML::Key << "End" << YAML::Value << loop->end;
        out << YAML::Key << "Count" << YAML::Value << loop->count;
        out << YAML::EndMap;
    }
    out << YAML::EndMap;

    const std::string yaml = out.c_str();
    Companion::Instance->RegisterCompanionFile(rel + ".yaml", std::vector<char>(yaml.begin(), yaml.end()));

    const std::vector<uint8_t> bin = BuildRaw(book, loop, adpcm);
    Companion::Instance->RegisterCompanionFile(rel + ".bin", std::vector<char>(bin.begin(), bin.end()));

    if (book != nullptr && !adpcm.empty()) {
        int32_t bankRate = 22050;
        for (const auto& [off, bank] : font.mBanks) {
            bankRate = bank.sampleRate;
            break;
        }
        const int rate = PlaybackRate(bankRate, keymap != font.mKeyMaps.end() ? &keymap->second : nullptr);
        const std::vector<uint8_t> wav = BuildWav(DecodeAdpcm(adpcm, *book), rate);
        Companion::Instance->RegisterCompanionFile(rel + ".wav", std::vector<char>(wav.begin(), wav.end()));
    }
}

} // namespace

ExportResult SoundfontModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto font = std::static_pointer_cast<SoundfontData>(raw);
    const SoundNaming naming = ReadNaming(node);
    *replacement += ".yaml";

    YAML::Emitter bank;
    bank << YAML::BeginMap;
    std::map<uint32_t, std::string> seen;

    for (uint32_t bankOffset : font->mBankOffsets) {
        const auto bankIt = font->mBanks.find(bankOffset);
        if (bankIt == font->mBanks.end()) {
            continue;
        }
        bank << YAML::Key << "SampleRate" << YAML::Value << bankIt->second.sampleRate;
        bank << YAML::Key << "Instruments" << YAML::Value << YAML::BeginSeq;

        for (size_t i = 0; i < bankIt->second.instrumentOffsets.size(); i++) {
            const auto inst = font->mInstruments.find(bankIt->second.instrumentOffsets[i]);
            bank << YAML::BeginMap;
            if (inst == font->mInstruments.end()) {
                bank << YAML::Key << "Present" << YAML::Value << false << YAML::EndMap;
                continue;
            }
            bank << YAML::Key << "Volume" << YAML::Value << (int)inst->second.volume;
            bank << YAML::Key << "Pan" << YAML::Value << (int)inst->second.pan;
            bank << YAML::Key << "Priority" << YAML::Value << (int)inst->second.priority;
            bank << YAML::Key << "BendRange" << YAML::Value << inst->second.bendRange;
            bank << YAML::Key << "Sounds" << YAML::Value << YAML::BeginSeq;

            for (size_t k = 0; k < inst->second.soundOffsets.size(); k++) {
                const uint32_t soundOffset = inst->second.soundOffsets[k];
                if (soundOffset == 0) {
                    bank << "";
                    continue;
                }
                auto known = seen.find(soundOffset);
                if (known == seen.end()) {
                    known = seen.emplace(soundOffset, PathForSound(naming, *font, i, k,
                                                                   inst->second.soundOffsets.size(), soundOffset))
                                .first;
                    const auto sound = font->mSounds.find(soundOffset);
                    if (sound != font->mSounds.end()) {
                        WriteSoundModFiles(*font, sound->second, known->second);
                    }
                }
                bank << known->second;
            }
            bank << YAML::EndSeq << YAML::EndMap;
        }
        bank << YAML::EndSeq;
    }
    bank << YAML::EndMap;

    write << bank.c_str();
    SPDLOG_INFO("Soundfont '{}': wrote {} editable sounds", entryName, seen.size());
    return std::nullopt;
}

namespace {

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

uint32_t ReadLE(const std::vector<uint8_t>& d, size_t& at, int bytes) {
    uint32_t value = 0;
    for (int i = 0; i < bytes; i++) {
        value |= static_cast<uint32_t>(at < d.size() ? d[at] : 0) << (i * 8);
        at++;
    }
    return value;
}

bool ParseRawSample(const std::vector<uint8_t>& d, SoundfontBook& book, std::vector<int16_t>& loopState,
                    std::vector<uint8_t>& adpcm) {
    if (d.size() < 12) {
        return false;
    }
    size_t at = 0;
    book.order = static_cast<int32_t>(ReadLE(d, at, 4));
    book.npredictors = static_cast<int32_t>(ReadLE(d, at, 4));
    const uint32_t bookCount = ReadLE(d, at, 4);
    book.book.clear();
    for (uint32_t i = 0; i < bookCount; i++) {
        book.book.push_back(static_cast<int16_t>(ReadLE(d, at, 2)));
    }
    const uint32_t stateCount = ReadLE(d, at, 4);
    loopState.clear();
    for (uint32_t i = 0; i < stateCount; i++) {
        loopState.push_back(static_cast<int16_t>(ReadLE(d, at, 2)));
    }
    const uint32_t dataSize = ReadLE(d, at, 4);
    if (at + dataSize > d.size()) {
        return false;
    }
    adpcm.assign(d.begin() + at, d.begin() + at + dataSize);
    return true;
}

int Field(const YAML::Node& n, const char* key, int def) {
    return n[key] ? n[key].as<int>() : def;
}

void TrimSilence(std::vector<int16_t>& pcm, int rate, double& leadSeconds, double& tailSeconds) {
    constexpr int16_t kFloor = 512;
    size_t first = 0;
    while (first < pcm.size() && std::abs(static_cast<int>(pcm[first])) <= kFloor) {
        first++;
    }
    if (first == pcm.size()) {
        leadSeconds = 0.0;
        tailSeconds = 0.0;
        return;
    }
    size_t last = pcm.size();
    while (last > first && std::abs(static_cast<int>(pcm[last - 1])) <= kFloor) {
        last--;
    }

    const size_t guard = static_cast<size_t>(rate / 500);
    first = first > guard ? first - guard : 0;
    last = std::min(pcm.size(), last + guard);

    leadSeconds = static_cast<double>(first) / static_cast<double>(rate);
    tailSeconds = static_cast<double>(pcm.size() - last) / static_cast<double>(rate);
    pcm = std::vector<int16_t>(pcm.begin() + first, pcm.begin() + last);
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> SoundfontFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                            YAML::Node& node) {
    YAML::Node bankNode;
    try {
        bankNode = YAML::Load(std::string(reinterpret_cast<char*>(buffer.data()), buffer.size()));
    } catch (const YAML::ParserException& envelope) {
        SPDLOG_ERROR("Soundfont: modding yaml is malformed: {}", envelope.what());
        return std::nullopt;
    }

    const std::filesystem::path root = std::filesystem::path(Companion::Instance->GetConfig().moddingPath) /
                                       Companion::Instance->GetCurrentDirectory();
    const int32_t sampleRate = bankNode["SampleRate"] ? bankNode["SampleRate"].as<int32_t>() : 22050;

    auto font = std::make_shared<SoundfontData>(std::vector<uint8_t>());
    uint32_t nextKey = 1;
    std::map<std::string, uint32_t> soundByPath;
    size_t encoded = 0;
    size_t verbatim = 0;

    SoundfontBank bank;
    bank.sampleRate = sampleRate;

    for (const auto& instNode : bankNode["Instruments"]) {
        if (instNode["Present"] && !instNode["Present"].as<bool>()) {
            bank.instrumentOffsets.push_back(0);
            continue;
        }
        SoundfontInstrument inst;
        inst.volume = static_cast<uint8_t>(Field(instNode, "Volume", 127));
        inst.pan = static_cast<uint8_t>(Field(instNode, "Pan", 64));
        inst.priority = static_cast<uint8_t>(Field(instNode, "Priority", 5));
        inst.bendRange = static_cast<int16_t>(Field(instNode, "BendRange", 200));

        for (const auto& entry : instNode["Sounds"]) {
            const std::string rel = entry.as<std::string>();
            if (rel.empty()) {
                inst.soundOffsets.push_back(0);
                continue;
            }
            const auto known = soundByPath.find(rel);
            if (known != soundByPath.end()) {
                inst.soundOffsets.push_back(known->second);
                continue;
            }

            YAML::Node sound;
            try {
                sound = YAML::LoadFile((root / (rel + ".yaml")).string());
            } catch (const std::exception& envelope) {
                SPDLOG_ERROR("Soundfont: cannot read {}.yaml: {}", rel, envelope.what());
                return std::nullopt;
            }

            SoundfontBook book;
            std::vector<int16_t> loopState;
            std::vector<uint8_t> adpcm;
            std::optional<int32_t> fittedDecay;
            const std::string mode = sound["Sample"] ? sound["Sample"].as<std::string>() : "raw";

            if (mode == "raw") {
                const std::vector<uint8_t> bin = ReadFile(root / (rel + ".bin"));
                if (bin.empty() || !ParseRawSample(bin, book, loopState, adpcm)) {
                    SPDLOG_ERROR("Soundfont: {}.bin is missing or malformed", rel);
                    return std::nullopt;
                }
                verbatim++;
            } else {
                const std::filesystem::path audio = root / std::filesystem::path(rel).parent_path() / mode;
                if (!std::filesystem::exists(audio)) {
                    SPDLOG_ERROR("Soundfont: {} names sample '{}', which is not next to it", rel, mode);
                    return std::nullopt;
                }
                SoundfontKeyMap slot;
                slot.keyBase = static_cast<uint8_t>(Field(sound, "KeyBase", 60));
                slot.detune = static_cast<int8_t>(Field(sound, "Detune", 0));

                const int playbackRate = PlaybackRate(sampleRate, &slot);
                std::vector<int16_t> pcm;
                std::string error;
                if (!DecodeAudioFile(audio.string(), playbackRate, pcm, error)) {
                    SPDLOG_ERROR("Soundfont: {}", error);
                    return std::nullopt;
                }

                const bool trim = sound["TrimSilence"] ? sound["TrimSilence"].as<bool>() : !sound["Loop"];
                if (trim) {
                    double lead = 0.0, tail = 0.0;
                    TrimSilence(pcm, playbackRate, lead, tail);
                    if (lead > 0.001 || tail > 0.001) {
                        SPDLOG_INFO("Soundfont: {} trimmed {:.3f}s of silence from the start and {:.3f}s "
                                    "from the end",
                                    rel, lead, tail);
                    }
                }

                const int64_t loopStart =
                    sound["Loop"] ? static_cast<int64_t>(sound["Loop"]["Start"].as<uint32_t>()) : -1;
                const VadpcmSample enc = EncodeVadpcm(pcm, 1, loopStart);
                book.order = enc.order;
                book.npredictors = enc.npredictors;
                book.book = enc.book;
                loopState = enc.loopState;
                adpcm = enc.frames;
                encoded++;
                SPDLOG_INFO("Soundfont: encoded {} from {} -- {} samples, {:.1f} dB", rel, audio.filename().string(),
                            pcm.size(), enc.snrDb);

                const bool fit = sound["FitEnvelope"] ? sound["FitEnvelope"].as<bool>() : true;
                if (fit && sound["Envelope"]) {
                    const auto envelope = sound["Envelope"];
                    const int64_t attack = envelope["AttackTime"].as<int64_t>();
                    const int64_t release = envelope["ReleaseTime"].as<int64_t>();
                    const int64_t was = envelope["DecayTime"].as<int64_t>();
                    const int64_t needed = static_cast<int64_t>(1000000.0 * static_cast<double>(pcm.size()) /
                                                                static_cast<double>(sampleRate));
                    const int64_t decay = std::max<int64_t>(0, needed - attack - release);
                    if (decay != was) {
                        fittedDecay = static_cast<int32_t>(decay);
                        SPDLOG_INFO("Soundfont: {} envelope fitted to the new sample -- DecayTime {} -> {} "
                                    "({:.3f}s)",
                                    rel, was, decay, static_cast<double>(needed) / 1e6);
                    }
                    if (needed < attack + release) {
                        SPDLOG_WARN("Soundfont: {} is shorter than its attack and release together; it will be "
                                    "clipped whatever the decay",
                                    rel);
                    }
                }
            }

            const uint32_t base = static_cast<uint32_t>((font->mSampleData.size() + 7) & ~static_cast<size_t>(7));
            font->mSampleData.resize(base);
            font->mSampleData.insert(font->mSampleData.end(), adpcm.begin(), adpcm.end());

            const uint32_t bookKey = nextKey++;
            font->mBooks.emplace(bookKey, book);

            uint32_t loopKey = 0;
            if (sound["Loop"]) {
                SoundfontLoop loop;
                loop.start = sound["Loop"]["Start"].as<uint32_t>();
                loop.end = sound["Loop"]["End"].as<uint32_t>();
                loop.count = sound["Loop"]["Count"].as<uint32_t>();
                loop.state = loopState.size() == 16 ? loopState : std::vector<int16_t>(16, 0);
                loopKey = nextKey++;
                font->mLoops.emplace(loopKey, loop);
            }

            SoundfontWave wave;
            wave.base = base;
            wave.len = static_cast<int32_t>(adpcm.size());
            wave.type = 0;
            wave.bookOffset = bookKey;
            wave.loopOffset = loopKey;
            const uint32_t waveKey = nextKey++;
            font->mWaves.emplace(waveKey, wave);

            SoundfontEnvelope env;
            if (sound["Envelope"]) {
                const auto envelope = sound["Envelope"];
                env.attackTime = envelope["AttackTime"].as<int32_t>();
                env.attackVolume = static_cast<uint8_t>(envelope["AttackVolume"].as<int>());
                env.decayTime = fittedDecay.value_or(envelope["DecayTime"].as<int32_t>());
                env.decayVolume = static_cast<uint8_t>(envelope["DecayVolume"].as<int>());
                env.releaseTime = envelope["ReleaseTime"].as<int32_t>();
            }
            const uint32_t envKey = nextKey++;
            font->mEnvelopes.emplace(envKey, env);
            SoundfontKeyMap keymap;
            const int chain = Field(sound, "ChainNext", 0);
            keymap.velocityMin = static_cast<uint8_t>(chain & 0xFF);
            keymap.velocityMax = static_cast<uint8_t>(Field(sound, "ChainDelayFrames", 0));
            keymap.keyMin =
                static_cast<uint8_t>((Field(sound, "VolumeGroup", 0) & 0x3F) | (((chain >> 8) & 0x03) << 6));
            keymap.keyMax =
                static_cast<uint8_t>((Field(sound, "ReverbSend", 0) & 0x0F) | (Field(sound, "KeyMaxFlags", 0) & 0xF0));
            keymap.keyBase = static_cast<uint8_t>(Field(sound, "KeyBase", 60));
            keymap.detune = static_cast<int8_t>(Field(sound, "Detune", 0));
            const uint32_t kmKey = nextKey++;
            font->mKeyMaps.emplace(kmKey, keymap);
            SoundfontSound record;
            record.envelopeOffset = envKey;
            record.keyMapOffset = kmKey;
            record.waveOffset = waveKey;
            record.samplePan = static_cast<uint8_t>(Field(sound, "SamplePan", 64));
            record.sampleVolume = static_cast<uint8_t>(Field(sound, "SampleVolume", 127));
            record.flags = static_cast<uint8_t>(Field(sound, "Flags", 0));
            const uint32_t soundKey = nextKey++;
            font->mSounds.emplace(soundKey, record);

            soundByPath.emplace(rel, soundKey);
            inst.soundOffsets.push_back(soundKey);
        }

        const uint32_t instKey = nextKey++;
        font->mInstruments.emplace(instKey, inst);
        bank.instrumentOffsets.push_back(instKey);
    }

    const uint32_t bankKey = nextKey++;
    font->mBanks.emplace(bankKey, bank);
    font->mBankOffsets.push_back(bankKey);
    font->mRevision = 0x4231;

    SPDLOG_INFO("Soundfont: rebuilt {} sounds from modding files -- {} re-encoded, {} verbatim", soundByPath.size(),
                encoded, verbatim);
    return font;
}

} // namespace BK64
