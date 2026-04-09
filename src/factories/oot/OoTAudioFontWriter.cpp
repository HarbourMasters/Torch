#ifdef OOT_SUPPORT

#include "OoTAudioFontWriter.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <sstream>

namespace OoT {

std::string AudioFontWriter::GetSampleRef(int bankIndex, uint32_t sampleAddr, uint32_t baseOffset,
                                          FontWriteContext& ctx) {
    if (sampleAddr + 16 > ctx.audioBank.Size()) return "";
    uint32_t samplePtr = ctx.audioBank.ReadU32(sampleAddr);
    if (samplePtr == 0) return "";
    samplePtr += baseOffset;
    if (samplePtr + 4 > ctx.audioBank.Size()) return "";
    uint32_t dataRelPtr = ctx.audioBank.ReadU32(samplePtr + 4);
    uint32_t absOffset = dataRelPtr + ctx.sampleBankTable[bankIndex].ptr;
    if (ctx.sampleMap.count(absOffset)) {
        return "audio/samples/" + ctx.sampleMap[absOffset].name + "_META";
    }
    return "";
}

std::vector<std::pair<int16_t, int16_t>> AudioFontWriter::ParseEnvelope(uint32_t envOffset, FontWriteContext& ctx) {
    std::vector<std::pair<int16_t, int16_t>> envs;
    while (envOffset + 4 <= ctx.audioBank.Size()) {
        int16_t delay = ctx.audioBank.ReadS16(envOffset);
        int16_t arg = ctx.audioBank.ReadS16(envOffset + 2);
        envs.push_back({delay, arg});
        envOffset += 4;
        if (delay < 0) break;
    }
    return envs;
}

void AudioFontWriter::WriteEnvData(LUS::BinaryWriter& w, const std::vector<std::pair<int16_t, int16_t>>& envs) {
    w.Write(static_cast<uint32_t>(envs.size()));
    for (auto& [delay, arg] : envs) {
        w.Write(delay);
        w.Write(arg);
    }
}

void AudioFontWriter::WriteSFE(LUS::BinaryWriter& w, uint32_t sfeOffset, uint32_t baseOffset,
                               int bankIndex, FontWriteContext& ctx) {
    if (sfeOffset + 8 > ctx.audioBank.Size()) {
        w.Write(static_cast<uint8_t>(0));
        return;
    }
    uint32_t samplePtr = ctx.audioBank.ReadU32(sfeOffset);
    if (samplePtr == 0) {
        w.Write(static_cast<uint8_t>(0));
        return;
    }
    w.Write(static_cast<uint8_t>(1)); // exists
    w.Write(static_cast<uint8_t>(1)); // padding (V2 compat)

    samplePtr += baseOffset;
    if (samplePtr + 4 <= ctx.audioBank.Size()) {
        uint32_t dataRelPtr = ctx.audioBank.ReadU32(samplePtr + 4);
        uint32_t absOffset = dataRelPtr + ctx.sampleBankTable[bankIndex].ptr;
        if (ctx.sampleMap.count(absOffset)) {
            w.Write(std::string("audio/samples/" + ctx.sampleMap[absOffset].name + "_META"));
        } else {
            w.Write(std::string(""));
        }
    } else {
        w.Write(std::string(""));
    }
    w.Write(ctx.audioBank.ReadFloat(sfeOffset + 4));
}

void FontResidue::Reset() {
    mLoaded = mRangeLo = mRangeHi = mRelease = 0;
}

// Seed from last drum's stack layout (DrumEntry→InstrumentEntry mapping).
void FontResidue::SeedFromDrums(const std::vector<DrumEntry>& drums) {
    if (drums.empty()) return;
    auto& lastDrum = drums.back();
    mLoaded = lastDrum.pan;      // drum.pan (offset 1) → inst.loaded (offset 1)
    mRangeLo = lastDrum.loaded;  // drum.loaded (offset 2) → inst.normalRangeLo (offset 2)
    mRangeHi = 0;                // padding (offset 3)
    mRelease = 0;                // drum.offset=0 (offset 4)
}

void FontResidue::ApplyToInstrument(InstEntry& inst) const {
    inst.loaded = mLoaded;
    inst.normalRangeLo = mRangeLo;
    inst.normalRangeHi = mRangeHi;
    inst.releaseRate = mRelease;
}

void FontResidue::UpdateFromInstrument(const InstEntry& inst) {
    mLoaded = inst.loaded;
    mRangeLo = inst.normalRangeLo;
    mRangeHi = inst.normalRangeHi;
    mRelease = inst.releaseRate;
}

std::vector<InstEntry> AudioFontWriter::ParseInstruments(int numInstruments, uint32_t ptr,
                                                         FontWriteContext& ctx) {
    std::vector<InstEntry> instruments;
    for (int i = 0; i < numInstruments; i++) {
        if (ptr + 8 + (i + 1) * 4 > ctx.audioBank.Size()) break;
        uint32_t instPtr = ctx.audioBank.ReadU32(ptr + 8 + i * 4);
        InstEntry inst = {};
        inst.isValid = (instPtr != 0);
        mResidue.ApplyToInstrument(inst);
        if (instPtr != 0) {
            instPtr += ptr;
            if (instPtr + 28 <= ctx.audioBank.Size()) {
                inst.loaded = ctx.audioBank.ReadU8(instPtr);
                inst.normalRangeLo = ctx.audioBank.ReadU8(instPtr + 1);
                inst.normalRangeHi = ctx.audioBank.ReadU8(instPtr + 2);
                inst.releaseRate = ctx.audioBank.ReadU8(instPtr + 3);
                inst.env = ParseEnvelope(ctx.audioBank.ReadU32(instPtr + 4) + ptr, ctx);
                inst.lowAddr = instPtr + 8;
                inst.normalAddr = instPtr + 16;
                inst.highAddr = instPtr + 24;
                mResidue.UpdateFromInstrument(inst);
            }
        }
        instruments.push_back(inst);
    }

    return instruments;
}

std::vector<SFXEntry> AudioFontWriter::ParseSFX(int numSfx, uint32_t ptr, int sampleBankId,
                                                FontWriteContext& ctx) {
    std::vector<SFXEntry> sfxEntries;
    if (ptr + 8 > ctx.audioBank.Size()) return sfxEntries;

    uint32_t sfxListAddr = ctx.audioBank.ReadU32(ptr + 4) + ptr;
    for (int i = 0; i < numSfx; i++) {
        uint32_t sfeAddr = sfxListAddr + i * 8;
        if (sfeAddr + 8 > ctx.audioBank.Size()) break;
        uint32_t sp = ctx.audioBank.ReadU32(sfeAddr);
        if (sp != 0) {
            sp += ptr;
            std::string ref;
            if (sp + 4 <= ctx.audioBank.Size()) {
                uint32_t relPtr = ctx.audioBank.ReadU32(sp + 4);
                uint32_t absOff = relPtr + ctx.sampleBankTable[sampleBankId].ptr;
                if (ctx.sampleMap.count(absOff))
                    ref = "audio/samples/" + ctx.sampleMap[absOff].name + "_META";
            }
            sfxEntries.push_back({true, ref, ctx.audioBank.ReadFloat(sfeAddr + 4)});
        } else {
            sfxEntries.push_back({false, "", ctx.audioBank.ReadFloat(sfeAddr + 4)});
        }
    }
    return sfxEntries;
}

std::vector<DrumEntry> AudioFontWriter::ParseDrums(int numDrums, uint32_t ptr, int sampleBankId,
                                                   FontWriteContext& ctx) {
    std::vector<DrumEntry> drums;
    if (ptr + 4 > ctx.audioBank.Size()) return drums;

    uint32_t drumListAddr = ctx.audioBank.ReadU32(ptr) + ptr;
    for (int i = 0; i < numDrums; i++) {
        if (drumListAddr + (i + 1) * 4 > ctx.audioBank.Size()) break;
        uint32_t drumPtr = ctx.audioBank.ReadU32(drumListAddr + i * 4);
        if (drumPtr != 0) {
            drumPtr += ptr;
            if (drumPtr + 16 <= ctx.audioBank.Size()) {
                DrumEntry d;
                d.releaseRate = ctx.audioBank.ReadU8(drumPtr);
                d.pan = ctx.audioBank.ReadU8(drumPtr + 1);
                d.loaded = ctx.audioBank.ReadU8(drumPtr + 2);
                d.tuning = ctx.audioBank.ReadFloat(drumPtr + 8);
                d.env = ParseEnvelope(ctx.audioBank.ReadU32(drumPtr + 12) + ptr, ctx);

                uint32_t sampleEntryPtr = ctx.audioBank.ReadU32(drumPtr + 4) + ptr;
                if (sampleEntryPtr + 4 <= ctx.audioBank.Size()) {
                    uint32_t dataRelPtr = ctx.audioBank.ReadU32(sampleEntryPtr + 4);
                    uint32_t absOffset = dataRelPtr + ctx.sampleBankTable[sampleBankId].ptr;
                    if (ctx.sampleMap.count(absOffset)) {
                        d.sampleRef = "audio/samples/" + ctx.sampleMap[absOffset].name + "_META";
                    }
                }
                drums.push_back(d);
                continue;
            }
        }
        drums.push_back({0, 0, 0, 0.0f, {}, ""});
    }
    mResidue.SeedFromDrums(drums);
    return drums;
}

void AudioFontWriter::WriteDrums(LUS::BinaryWriter& w, const std::vector<DrumEntry>& drums) {
    for (auto& d : drums) {
        w.Write(d.releaseRate);
        w.Write(d.pan);
        w.Write(d.loaded);
        WriteEnvData(w, d.env);
        w.Write(static_cast<uint8_t>(d.sampleRef.empty() ? 0 : 1));
        w.Write(d.sampleRef);
        w.Write(d.tuning);
    }
}

void AudioFontWriter::WriteInstruments(LUS::BinaryWriter& w, const std::vector<InstEntry>& instruments,
                                       uint32_t ptr, int sampleBankId, FontWriteContext& ctx) {
    for (auto& inst : instruments) {
        w.Write(static_cast<uint8_t>(inst.isValid ? 1 : 0));
        w.Write(inst.loaded);
        w.Write(inst.normalRangeLo);
        w.Write(inst.normalRangeHi);
        w.Write(inst.releaseRate);
        WriteEnvData(w, inst.env);

        if (inst.isValid) {
            if (ctx.audioBank.ReadU32(inst.lowAddr) != 0) {
                WriteSFE(w, inst.lowAddr, ptr, sampleBankId, ctx);
            } else {
                w.Write(static_cast<uint8_t>(0));
            }
            if (ctx.audioBank.ReadU32(inst.normalAddr) != 0) {
                WriteSFE(w, inst.normalAddr, ptr, sampleBankId, ctx);
            } else {
                w.Write(static_cast<uint8_t>(0));
            }
            if (ctx.audioBank.ReadU32(inst.highAddr) != 0 && inst.normalRangeHi != 0x7F) {
                WriteSFE(w, inst.highAddr, ptr, sampleBankId, ctx);
            } else {
                w.Write(static_cast<uint8_t>(0));
            }
        } else {
            w.Write(static_cast<uint8_t>(0));
            w.Write(static_cast<uint8_t>(0));
            w.Write(static_cast<uint8_t>(0));
        }
    }
}

void AudioFontWriter::WriteSFXEntries(LUS::BinaryWriter& w, const std::vector<SFXEntry>& sfxEntries) {
    for (auto& sfx : sfxEntries) {
        if (sfx.exists) {
            w.Write(static_cast<uint8_t>(1));
            w.Write(static_cast<uint8_t>(1));
            w.Write(sfx.sampleRef);
            w.Write(sfx.tuning);
        } else {
            w.Write(static_cast<uint8_t>(0));
        }
    }
}

void AudioFontWriter::WriteFontCompanion(uint32_t fontIndex, const AudioTableEntry& fontEntry,
                                         const std::vector<DrumEntry>& drums,
                                         const std::vector<InstEntry>& instruments,
                                         const std::vector<SFXEntry>& sfxEntries,
                                         uint32_t ptr, int sampleBankId,
                                         FontWriteContext& ctx,
                                         const std::map<int, std::string>& fontNames) {
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudioSoundFont, 2);

    // Font metadata
    w.Write(static_cast<uint32_t>(fontIndex));
    w.Write(fontEntry.medium);
    w.Write(fontEntry.cachePolicy);
    w.Write(fontEntry.data1);
    w.Write(fontEntry.data2);
    w.Write(fontEntry.data3);

    w.Write(static_cast<uint32_t>(drums.size()));
    w.Write(static_cast<uint32_t>(instruments.size()));
    w.Write(static_cast<uint32_t>(sfxEntries.size()));

    WriteDrums(w, drums);
    WriteInstruments(w, instruments, ptr, sampleBankId, ctx);
    WriteSFXEntries(w, sfxEntries);

    std::string fontName;
    if (fontNames.count(fontIndex)) {
        fontName = fontNames.at(fontIndex);
    } else {
        fontName = std::to_string(fontIndex) + "_Font";
    }

    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    Companion::Instance->RegisterCompanionFile(
        "fonts/" + fontName, std::vector<char>(str.begin(), str.end()));
}

void AudioFontWriter::Extract(YAML::Node& node,
                              SafeAudioBankReader& audioBank,
                              const std::vector<AudioTableEntry>& fontTable,
                              const std::vector<AudioTableEntry>& sampleBankTable,
                              std::map<uint32_t, SampleInfo>& sampleMap) {
    // Build font name map from YAML
    std::map<int, std::string> fontNames;
    if (node["fonts"] && node["fonts"].IsSequence()) {
        auto fontsNode = node["fonts"];
        for (size_t i = 0; i < fontsNode.size(); i++) {
            auto fn = fontsNode[i];
            fontNames[fn["index"].as<int>()] = fn["name"].as<std::string>();
        }
    }

    FontWriteContext ctx {
        .audioBank = audioBank,
        .sampleBankTable = sampleBankTable,
        .sampleMap = sampleMap,
    };

    mResidue.Reset();

    for (uint32_t fi = 0; fi < fontTable.size(); fi++) {
        auto& fe = fontTable[fi];
        uint32_t ptr = fe.ptr;
        int sampleBankId = (fe.data1 >> 8) & 0xFF;
        int numInstruments = (fe.data2 >> 8) & 0xFF;
        int numDrums = fe.data2 & 0xFF;
        int numSfx = fe.data3;

        auto drums = ParseDrums(numDrums, ptr, sampleBankId, ctx);
        auto sfxEntries = ParseSFX(numSfx, ptr, sampleBankId, ctx);
        auto instruments = ParseInstruments(numInstruments, ptr, ctx);

        WriteFontCompanion(fi, fe, drums, instruments, sfxEntries, ptr, sampleBankId, ctx, fontNames);
    }

    SPDLOG_INFO("OoTAudioFactory: wrote {} font companion files", fontTable.size());
}

} // namespace OoT

#endif
