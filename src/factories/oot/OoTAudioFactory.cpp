#ifdef OOT_SUPPORT

#include "OoTAudioFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <map>
#include <iomanip>

namespace OoT {

SafeAudioBankReader::SafeAudioBankReader(const std::vector<uint8_t>& data)
    : mData(data), mReader((char*)data.data(), data.size()) {
    mReader.SetEndianness(Torch::Endianness::Big);
}

uint32_t SafeAudioBankReader::ReadU32(uint32_t offset) {
    if (offset + 4 > mData.size()) return 0;
    mReader.Seek(offset, LUS::SeekOffsetType::Start);
    return mReader.ReadUInt32();
}

int16_t SafeAudioBankReader::ReadS16(uint32_t offset) {
    if (offset + 2 > mData.size()) return 0;
    mReader.Seek(offset, LUS::SeekOffsetType::Start);
    return mReader.ReadInt16();
}

float SafeAudioBankReader::ReadFloat(uint32_t offset) {
    if (offset + 4 > mData.size()) return 0.0f;
    mReader.Seek(offset, LUS::SeekOffsetType::Start);
    return mReader.ReadFloat();
}

// Parse an audio table header from decompressed code segment
std::vector<AudioTableEntry> OoTAudioFactory::ParseAudioTable(const uint8_t* codeData, uint32_t tableOffset) {
    LUS::BinaryReader reader((char*)(codeData + tableOffset), 0x10000);
    reader.SetEndianness(Torch::Endianness::Big);

    uint16_t numEntries = reader.ReadUInt16();
    reader.ReadUInt16(); // padding
    reader.ReadUInt32(); // romAddr (unused)
    reader.ReadUInt32(); // padding
    reader.ReadUInt32(); // padding

    std::vector<AudioTableEntry> entries;
    entries.reserve(numEntries);
    for (uint16_t i = 0; i < numEntries; i++) {
        AudioTableEntry e;
        e.ptr = reader.ReadUInt32();
        e.size = reader.ReadUInt32();
        e.medium = reader.ReadUByte();
        e.cachePolicy = reader.ReadUByte();
        e.data1 = reader.ReadInt16();
        e.data2 = reader.ReadInt16();
        e.data3 = reader.ReadInt16();
        entries.push_back(e);
    }
    return entries;
}

// Parse sequence-to-font mapping table from decompressed code segment
std::vector<std::vector<uint8_t>> OoTAudioFactory::ParseSequenceFontTable(
    const uint8_t* codeData, uint32_t tableOffset, uint32_t numSequences) {
    std::vector<std::vector<uint8_t>> result;
    result.reserve(numSequences);

    // The table is: for each sequence, a 2-byte offset into a data area
    // Then the data area has: count byte + font indices
    LUS::BinaryReader reader((char*)(codeData + tableOffset), 0x10000);
    reader.SetEndianness(Torch::Endianness::Big);

    // Read offsets for each sequence
    std::vector<uint16_t> offsets;
    for (uint32_t i = 0; i < numSequences; i++) {
        offsets.push_back(reader.ReadUInt16());
    }

    // For each sequence, read the font indices at its offset
    for (uint32_t i = 0; i < numSequences; i++) {
        LUS::BinaryReader dataReader((char*)(codeData + tableOffset + offsets[i]), 256);
        dataReader.SetEndianness(Torch::Endianness::Big);
        uint8_t count = dataReader.ReadUByte();
        std::vector<uint8_t> fonts;
        for (uint8_t j = 0; j < count; j++) {
            fonts.push_back(dataReader.ReadUByte());
        }
        result.push_back(fonts);
    }
    return result;
}

std::vector<char> OoTAudioFactory::BuildMainAudioHeader() {
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudio, 2);
    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    return std::vector<char>(str.begin(), str.end());
}

void OoTAudioFactory::ParseSample(int bankIndex, uint32_t sampleAddr, uint32_t baseOffset, AudioParseContext& ctx) {
    if (sampleAddr + 16 > ctx.audioBankData.size()) return;

    uint32_t dataRelPtr = ctx.audioBank.ReadU32(sampleAddr + 4);
    uint32_t sampleDataOffset = dataRelPtr + ctx.sampleBankTable[bankIndex].ptr;
    if (ctx.sampleMap.count(sampleDataOffset)) return;

    SampleInfo s;
    uint32_t origField = ctx.audioBank.ReadU32(sampleAddr);
    s.codec = (origField >> 28) & 0x0F;
    s.medium = (origField >> 24) & 0x03;
    s.unk_bit26 = (origField >> 22) & 0x01;
    s.unk_bit25 = (origField >> 21) & 0x01;
    s.dataSize = origField & 0x00FFFFFF;
    s.dataOffset = sampleDataOffset;

    uint32_t loopAddr = ctx.audioBank.ReadU32(sampleAddr + 8) + baseOffset;
    uint32_t bookAddr = ctx.audioBank.ReadU32(sampleAddr + 12) + baseOffset;

    if (loopAddr + 12 <= ctx.audioBankData.size()) {
        s.loopStart = (int32_t)ctx.audioBank.ReadU32(loopAddr);
        s.loopEnd = (int32_t)ctx.audioBank.ReadU32(loopAddr + 4);
        s.loopCount = (int32_t)ctx.audioBank.ReadU32(loopAddr + 8);

        if (s.loopCount != 0 && loopAddr + 48 <= ctx.audioBankData.size()) {
            for (int i = 0; i < 16; i++) {
                s.loopStates.push_back(ctx.audioBank.ReadS16(loopAddr + 16 + i * 2));
            }
        }
    }

    if (bookAddr + 8 <= ctx.audioBankData.size()) {
        s.bookOrder = (int32_t)ctx.audioBank.ReadU32(bookAddr);
        s.bookNpredictors = (int32_t)ctx.audioBank.ReadU32(bookAddr + 4);
        int numBooks = s.bookOrder * s.bookNpredictors * 8;
        if (bookAddr + 8 + numBooks * 2 <= ctx.audioBankData.size()) {
            for (int i = 0; i < numBooks; i++) {
                s.books.push_back(ctx.audioBank.ReadS16(bookAddr + 8 + i * 2));
            }
        }
    }

    // Resolve name from YAML (use absolute offset as key, matching ZAPDTR behavior)
    if (ctx.sampleNames.count(bankIndex) && ctx.sampleNames[bankIndex].count((int)sampleDataOffset)) {
        s.name = ctx.sampleNames[bankIndex][(int)sampleDataOffset];
    } else {
        std::ostringstream ss;
        ss << "sample_" << bankIndex << "_" << std::setfill('0') << std::setw(8)
           << std::hex << std::uppercase << sampleDataOffset;
        s.name = ss.str();
    }

    ctx.sampleMap[sampleDataOffset] = s;
}

void OoTAudioFactory::ParseSFESample(int bankIndex, uint32_t sfeAddr, uint32_t baseOffset, AudioParseContext& ctx) {
    if (sfeAddr + 4 > ctx.audioBankData.size()) return;
    uint32_t samplePtr = ctx.audioBank.ReadU32(sfeAddr);
    if (samplePtr != 0) {
        ParseSample(bankIndex, samplePtr + baseOffset, baseOffset, ctx);
    }
}

bool OoTAudioFactory::ExtractSamples(std::vector<uint8_t>& buffer, YAML::Node& node,
                                     std::vector<uint8_t>& audioBankData, SafeAudioBankReader& audioBank,
                                     const std::vector<AudioTableEntry>& fontTable,
                                     const std::vector<AudioTableEntry>& sampleBankTable,
                                     std::map<uint32_t, SampleInfo>& sampleMap) {
    // Load Audiotable segment (only needed for sample data extraction)
    auto audiotableSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(3);
    if (!audiotableSeg.has_value()) {
        SPDLOG_ERROR("OoTAudioFactory: Audiotable segment not found");
        return false;
    }
    uint32_t tableOff = audiotableSeg.value();
    uint32_t tableSize = std::min((uint32_t)0x500000, (uint32_t)(buffer.size() - tableOff));
    std::vector<uint8_t> audioTable(buffer.begin() + tableOff, buffer.begin() + tableOff + tableSize);
    // Build sample name map from YAML
    std::map<int, std::map<int, std::string>> sampleNames;
    if (node["samples"] && node["samples"].IsSequence()) {
        auto samplesNode = node["samples"];
        for (size_t i = 0; i < samplesNode.size(); i++) {
            auto bankNode = samplesNode[i];
            int bank = bankNode["bank"].as<int>();
            if (bankNode["entries"] && bankNode["entries"].IsSequence()) {
                auto entries = bankNode["entries"];
                for (size_t j = 0; j < entries.size(); j++) {
                    auto e = entries[j];
                    sampleNames[bank][e["offset"].as<int>()] = e["name"].as<std::string>();
                }
            }
        }
    }

    AudioParseContext ctx {
        .audioBankData = audioBankData,
        .audioBank = audioBank,
        .sampleBankTable = sampleBankTable,
        .sampleNames = sampleNames,
        .sampleMap = sampleMap,
    };

    // Iterate all fonts to discover samples
    for (uint32_t fi = 0; fi < fontTable.size(); fi++) {
        auto& fe = fontTable[fi];
        uint32_t ptr = fe.ptr;
        int sampleBankId = (fe.data1 >> 8) & 0xFF;
        int numInstruments = (fe.data2 >> 8) & 0xFF;
        int numDrums = fe.data2 & 0xFF;
        int numSfx = fe.data3;

        if (ptr + 8 > ctx.audioBankData.size()) continue;

        // Drums
        uint32_t drumListAddr = ctx.audioBank.ReadU32(ptr) + ptr;
        for (int i = 0; i < numDrums; i++) {
            if (drumListAddr + (i + 1) * 4 > ctx.audioBankData.size()) break;
            uint32_t drumPtr = ctx.audioBank.ReadU32(drumListAddr + i * 4);
            if (drumPtr != 0) {
                drumPtr += ptr;
                if (drumPtr + 8 > ctx.audioBankData.size()) continue;
                uint32_t sampleEntryPtr = ctx.audioBank.ReadU32(drumPtr + 4) + ptr;
                ParseSample(sampleBankId, sampleEntryPtr, ptr, ctx);
            }
        }

        // SFX
        uint32_t sfxListAddr = ctx.audioBank.ReadU32(ptr + 4) + ptr;
        for (int i = 0; i < numSfx; i++) {
            if (sfxListAddr + (i + 1) * 8 > ctx.audioBankData.size()) break;
            ParseSFESample(sampleBankId, sfxListAddr + i * 8, ptr, ctx);
        }

        // Instruments
        for (int i = 0; i < numInstruments; i++) {
            if (ptr + 8 + (i + 1) * 4 > ctx.audioBankData.size()) break;
            uint32_t instPtr = ctx.audioBank.ReadU32(ptr + 8 + i * 4);
            if (instPtr != 0) {
                instPtr += ptr;
                if (instPtr + 28 > ctx.audioBankData.size()) continue;
                ParseSFESample(sampleBankId, instPtr + 8, ptr, ctx);
                ParseSFESample(sampleBankId, instPtr + 16, ptr, ctx);
                ParseSFESample(sampleBankId, instPtr + 24, ptr, ctx);
            }
        }
    }

    SPDLOG_INFO("OoTAudioFactory: discovered {} unique samples", sampleMap.size());

    // Write sample companion files
    for (auto& [offset, s] : sampleMap) {
        LUS::BinaryWriter w;
        BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudioSample, 2);

        w.Write(s.codec);
        w.Write(s.medium);
        w.Write(s.unk_bit26);
        w.Write(s.unk_bit25);
        w.Write(static_cast<uint32_t>(s.dataSize));
        if (s.dataOffset + s.dataSize <= audioTable.size()) {
            w.Write((char*)(audioTable.data() + s.dataOffset), s.dataSize);
        }

        w.Write(static_cast<uint32_t>(s.loopStart));
        w.Write(static_cast<uint32_t>(s.loopEnd));
        w.Write(static_cast<uint32_t>(s.loopCount));
        w.Write(static_cast<uint32_t>(s.loopStates.size()));
        for (auto ls : s.loopStates) w.Write(ls);

        w.Write(static_cast<uint32_t>(s.bookOrder));
        w.Write(static_cast<uint32_t>(s.bookNpredictors));
        w.Write(static_cast<uint32_t>(s.books.size()));
        for (auto b : s.books) w.Write(b);

        std::stringstream ss;
        w.Finish(ss);
        std::string str = ss.str();
        Companion::Instance->RegisterCompanionFile(
            "samples/" + s.name + "_META", std::vector<char>(str.begin(), str.end()));
    }

    SPDLOG_INFO("OoTAudioFactory: wrote {} sample companion files", sampleMap.size());
    return true;
}

void OoTAudioFactory::WriteSequenceCompanion(const uint8_t* seqData, uint32_t seqSize,
                                              uint32_t originalIndex, uint8_t medium, uint8_t cachePolicy,
                                              const std::vector<uint8_t>& fonts, const std::string& seqName) {
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudioSequence, 2);

    w.Write(seqSize);
    w.Write((char*)seqData, seqSize);

    w.Write(static_cast<uint8_t>(originalIndex));
    w.Write(medium);
    w.Write(cachePolicy);

    w.Write(static_cast<uint32_t>(fonts.size()));
    for (auto f : fonts) {
        w.Write(f);
    }

    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    Companion::Instance->RegisterCompanionFile(
        "sequences/" + seqName, std::vector<char>(str.begin(), str.end()));
}

bool OoTAudioFactory::ExtractSequences(std::vector<uint8_t>& buffer, YAML::Node& node,
                                       const std::vector<AudioTableEntry>& seqTable,
                                       const std::vector<std::vector<uint8_t>>& seqFontMap) {
    // Get Audioseq ROM data (segment 2, uncompressed)
    auto audioseqSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(2);
    if (!audioseqSeg.has_value()) {
        SPDLOG_ERROR("OoTAudioFactory: Audioseq segment not found");
        return false;
    }
    uint32_t audioseqOff = audioseqSeg.value();

    // Get sequence names from YAML
    std::vector<std::string> seqNames;
    if (node["sequences"] && node["sequences"].IsSequence()) {
        auto seqNode = node["sequences"];
        for (size_t i = 0; i < seqNode.size(); i++) {
            seqNames.push_back(seqNode[i].as<std::string>());
        }
    }

    for (uint32_t i = 0; i < seqTable.size(); i++) {
        auto medium = seqTable[i].medium;
        auto cachePolicy = seqTable[i].cachePolicy;
        auto& fonts = seqFontMap[i];

        // Resolve alias: size==0 means ptr holds the index of the real sequence
        bool isAlias = (seqTable[i].size == 0);
        uint32_t dataIdx = isAlias ? seqTable[i].ptr : i;
        if (dataIdx >= seqTable.size()) {
            SPDLOG_WARN("OoTAudioFactory: sequence {} alias index {} out of range", i, dataIdx);
            continue;
        }
        auto& dataEntry = seqTable[dataIdx];

        // Sequence name
        std::string seqName;
        if (i < seqNames.size()) {
            seqName = seqNames[i];
        } else {
            std::ostringstream ss;
            ss << std::setfill('0') << std::setw(3) << i << "_Sequence";
            seqName = ss.str();
        }

        // Resolve sequence data location in ROM (using dataEntry for aliased sequences)
        uint32_t seqDataOff = audioseqOff + dataEntry.ptr;
        if (seqDataOff + dataEntry.size > buffer.size()) {
            SPDLOG_WARN("OoTAudioFactory: sequence {} out of bounds", seqName);
            continue;
        }

        WriteSequenceCompanion(buffer.data() + seqDataOff, dataEntry.size,
                               i, medium, cachePolicy, fonts, seqName);
    }

    SPDLOG_INFO("OoTAudioFactory: wrote {} sequence companion files", seqTable.size());
    return true;
}

std::optional<std::shared_ptr<IParsedData>> OoTAudioFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto data = std::make_shared<OoTAudioData>();
    data->mMainEntry = BuildMainAudioHeader();

    // Decompress the code segment (segment 128)
    auto codeDecoded = Decompressor::AutoDecode(
        node["offset"].as<uint32_t>(),
        0x200000, // max code segment size
        buffer);

    // Parse audio tables from code segment at YAML-specified offsets
    auto seqTable = ParseAudioTable(codeDecoded.segment.data, GetSafeNode<uint32_t>(node, "sequence_table_offset"));
    auto fontTable = ParseAudioTable(codeDecoded.segment.data, GetSafeNode<uint32_t>(node, "sound_font_table_offset"));
    auto sampleBankTable = ParseAudioTable(codeDecoded.segment.data, GetSafeNode<uint32_t>(node, "sample_bank_table_offset"));
    auto seqFontMap = ParseSequenceFontTable(codeDecoded.segment.data, GetSafeNode<uint32_t>(node, "sequence_font_table_offset"), seqTable.size());

    SPDLOG_INFO("OoTAudioFactory: {} sequences, {} fonts, {} sample banks",
                seqTable.size(), fontTable.size(), sampleBankTable.size());

    if (!ExtractSequences(buffer, node, seqTable, seqFontMap)) {
        return data;
    }

    // Load Audiobank segment (shared by samples and fonts)
    auto audiobankSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(1);
    if (!audiobankSeg.has_value()) {
        SPDLOG_ERROR("OoTAudioFactory: Audiobank segment not found");
        return data;
    }
    uint32_t bankOff = audiobankSeg.value();
    uint32_t bankSize = std::min((uint32_t)0x40000, (uint32_t)(buffer.size() - bankOff));
    std::vector<uint8_t> audioBankData(buffer.begin() + bankOff, buffer.begin() + bankOff + bankSize);
    SafeAudioBankReader audioBank(audioBankData);

    std::map<uint32_t, SampleInfo> sampleMap;
    if (!ExtractSamples(buffer, node, audioBankData, audioBank, fontTable, sampleBankTable, sampleMap)) {
        return data;
    }


    ExtractFonts(node, audioBankData, audioBank, fontTable, sampleBankTable, sampleMap);

    return data;
}

void OoTAudioFactory::ExtractFonts(YAML::Node& node,
                                   std::vector<uint8_t>& audioBankData, SafeAudioBankReader& audioBank,
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

    // Helper: resolve sample reference path (matches OTRExporter GetSampleEntryReference)
    auto getSampleRef = [&](int bankIndex, uint32_t sampleAddr, uint32_t baseOffset) -> std::string {
        if (sampleAddr + 16 > audioBankData.size()) return "";
        uint32_t samplePtr = audioBank.ReadU32(sampleAddr);
        if (samplePtr == 0) return "";
        samplePtr += baseOffset;
        if (samplePtr + 4 > audioBankData.size()) return "";
        uint32_t dataRelPtr = audioBank.ReadU32(samplePtr + 4);
        uint32_t absOffset = dataRelPtr + sampleBankTable[bankIndex].ptr;
        if (sampleMap.count(absOffset)) {
            return "audio/samples/" + sampleMap[absOffset].name + "_META";
        }
        return "";
    };

    // Helper: parse envelope data (ZAudio.cpp:74-93)
    auto parseEnvelope = [&](uint32_t envOffset) -> std::vector<std::pair<int16_t, int16_t>> {
        std::vector<std::pair<int16_t, int16_t>> envs;
        while (envOffset + 4 <= audioBankData.size()) {
            int16_t delay = audioBank.ReadS16(envOffset);
            int16_t arg = audioBank.ReadS16(envOffset + 2);
            envs.push_back({delay, arg});
            envOffset += 4;
            if (delay < 0) break;
        }
        return envs;
    };

    // Helper: write envelope data
    auto writeEnvData = [](LUS::BinaryWriter& w, const std::vector<std::pair<int16_t, int16_t>>& envs) {
        w.Write(static_cast<uint32_t>(envs.size()));
        for (auto& [delay, arg] : envs) {
            w.Write(delay);
            w.Write(arg);
        }
    };

    // Helper: write SoundFontEntry (AudioExporter.cpp:140-151)
    auto writeSFE = [&](LUS::BinaryWriter& w, uint32_t sfeOffset, uint32_t baseOffset,
                         int bankIndex) {
        if (sfeOffset + 8 > audioBankData.size()) {
            w.Write(static_cast<uint8_t>(0)); // exists = false
            return;
        }
        uint32_t samplePtr = audioBank.ReadU32(sfeOffset);
        if (samplePtr == 0) {
            w.Write(static_cast<uint8_t>(0)); // exists = false
            return;
        }
        w.Write(static_cast<uint8_t>(1)); // exists = true
        w.Write(static_cast<uint8_t>(1)); // padding byte (V2 compat)

        // Resolve sample reference
        samplePtr += baseOffset;
        if (samplePtr + 4 <= audioBankData.size()) {
            uint32_t dataRelPtr = audioBank.ReadU32(samplePtr + 4);
            uint32_t absOffset = dataRelPtr + sampleBankTable[bankIndex].ptr;
            if (sampleMap.count(absOffset)) {
                w.Write(std::string("audio/samples/" + sampleMap[absOffset].name + "_META"));
            } else {
                w.Write(std::string(""));
            }
        } else {
            w.Write(std::string(""));
        }
        w.Write(audioBank.ReadFloat(sfeOffset + 4)); // tuning
    };

    // Cross-font stack residue: ZAPDTR's ParseSoundFont is called per font,
    // reusing the same stack frame. Invalid instruments before any valid one
    // inherit residue from the previous font's last valid instrument.
    uint8_t crossFontLoaded = 0, crossFontRangeLo = 0, crossFontRangeHi = 0, crossFontRelease = 0;

    for (uint32_t fi = 0; fi < fontTable.size(); fi++) {
        auto& fe = fontTable[fi];
        uint32_t ptr = fe.ptr;
        int sampleBankId = (fe.data1 >> 8) & 0xFF;
        int numInstruments = (fe.data2 >> 8) & 0xFF;
        int numDrums = fe.data2 & 0xFF;
        int numSfx = fe.data3;

        LUS::BinaryWriter w;
        BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudioSoundFont, 2);

        // Font metadata
        w.Write(static_cast<uint32_t>(fi));
        w.Write(fe.medium);
        w.Write(fe.cachePolicy);
        w.Write(fe.data1);
        w.Write(fe.data2);
        w.Write(fe.data3);

        // Actual counts from parsing (may differ from table hints)
        // Parse drums
        std::vector<std::tuple<uint8_t, uint8_t, uint8_t, float,
                               std::vector<std::pair<int16_t, int16_t>>,
                               std::string>> drums;
        if (ptr + 4 <= audioBankData.size()) {
            uint32_t drumListAddr = audioBank.ReadU32(ptr) + ptr;
            for (int i = 0; i < numDrums; i++) {
                if (drumListAddr + (i + 1) * 4 > audioBankData.size()) break;
                uint32_t drumPtr = audioBank.ReadU32(drumListAddr + i * 4);
                if (drumPtr != 0) {
                    drumPtr += ptr;
                    if (drumPtr + 16 <= audioBankData.size()) {
                        uint8_t releaseRate = audioBankData[drumPtr];
                        uint8_t pan = audioBankData[drumPtr + 1];
                        uint8_t loaded = audioBankData[drumPtr + 2];
                        float tuning = audioBank.ReadFloat(drumPtr + 8);
                        auto env = parseEnvelope(audioBank.ReadU32(drumPtr + 12) + ptr);

                        // Resolve sample
                        uint32_t sampleEntryPtr = audioBank.ReadU32(drumPtr + 4) + ptr;
                        std::string sampleRef;
                        if (sampleEntryPtr + 4 <= audioBankData.size()) {
                            uint32_t dataRelPtr = audioBank.ReadU32(sampleEntryPtr + 4);
                            uint32_t absOffset = dataRelPtr + sampleBankTable[sampleBankId].ptr;
                            if (sampleMap.count(absOffset)) {
                                sampleRef = "audio/samples/" + sampleMap[absOffset].name + "_META";
                            }
                        }
                        drums.push_back({releaseRate, pan, loaded, tuning, env, sampleRef});
                        continue;
                    }
                }
                // Null/invalid drum
                drums.push_back({0, 0, 0, 0.0f, {}, ""});
            }
        }

        // Parse SFX
        struct SFXEntry {
            bool exists;
            std::string sampleRef;
            float tuning;
        };
        std::vector<SFXEntry> sfxEntries;
        if (ptr + 8 <= audioBankData.size()) {
            uint32_t sfxListAddr = audioBank.ReadU32(ptr + 4) + ptr;
            for (int i = 0; i < numSfx; i++) {
                uint32_t sfeAddr = sfxListAddr + i * 8;
                if (sfeAddr + 8 > audioBankData.size()) break;
                uint32_t sp = audioBank.ReadU32(sfeAddr);
                if (sp != 0) {
                    sp += ptr;
                    std::string ref;
                    if (sp + 4 <= audioBankData.size()) {
                        uint32_t relPtr = audioBank.ReadU32(sp + 4);
                        uint32_t absOff = relPtr + sampleBankTable[sampleBankId].ptr;
                        if (sampleMap.count(absOff))
                            ref = "audio/samples/" + sampleMap[absOff].name + "_META";
                    }
                    sfxEntries.push_back({true, ref, audioBank.ReadFloat(sfeAddr + 4)});
                } else {
                    sfxEntries.push_back({false, "", audioBank.ReadFloat(sfeAddr + 4)});
                }
            }
        }

        // Parse instruments
        struct InstEntry {
            bool isValid;
            uint8_t loaded, normalRangeLo, normalRangeHi, releaseRate;
            std::vector<std::pair<int16_t, int16_t>> env;
            // low/normal/high sound entries stored as raw offsets for writeSFE
            uint32_t lowAddr, normalAddr, highAddr;
        };
        std::vector<InstEntry> instruments;
        // ZAPDTR uses `InstrumentEntry instrument;` (not zero-initialized) in a loop.
        // POD fields retain stack residue from the previous iteration.
        // Track last-valid values to replicate this behavior for invalid instruments.
        uint8_t lastLoaded = crossFontLoaded, lastNormalRangeLo = crossFontRangeLo;
        uint8_t lastNormalRangeHi = crossFontRangeHi, lastReleaseRate = crossFontRelease;
        // Seed from last drum (DrumEntry→InstrumentEntry stack mapping).
        // See docs/oot-audio-font-residue-analysis.md
        if (!drums.empty()) {
            auto& [rr, pan, loaded_d, tuning_d, env_d, ref_d] = drums.back();
            lastLoaded = pan;            // drum.pan (offset 1) → inst.loaded (offset 1)
            lastNormalRangeLo = loaded_d; // drum.loaded (offset 2) → inst.normalRangeLo (offset 2)
            lastNormalRangeHi = 0;        // padding (offset 3)
            lastReleaseRate = 0;          // drum.offset=0 (offset 4)
        }
        for (int i = 0; i < numInstruments; i++) {
            if (ptr + 8 + (i + 1) * 4 > audioBankData.size()) break;
            uint32_t instPtr = audioBank.ReadU32(ptr + 8 + i * 4);
            InstEntry inst = {};
            inst.isValid = (instPtr != 0);
            // For invalid instruments, carry forward last valid's field values (stack residue)
            inst.loaded = lastLoaded;
            inst.normalRangeLo = lastNormalRangeLo;
            inst.normalRangeHi = lastNormalRangeHi;
            inst.releaseRate = lastReleaseRate;
            if (instPtr != 0) {
                instPtr += ptr;
                if (instPtr + 28 <= audioBankData.size()) {
                    inst.loaded = audioBankData[instPtr];
                    inst.normalRangeLo = audioBankData[instPtr + 1];
                    inst.normalRangeHi = audioBankData[instPtr + 2];
                    inst.releaseRate = audioBankData[instPtr + 3];
                    inst.env = parseEnvelope(audioBank.ReadU32(instPtr + 4) + ptr);
                    inst.lowAddr = instPtr + 8;
                    inst.normalAddr = instPtr + 16;
                    inst.highAddr = instPtr + 24;
                    // Update stack residue tracking
                    lastLoaded = inst.loaded;
                    lastNormalRangeLo = inst.normalRangeLo;
                    lastNormalRangeHi = inst.normalRangeHi;
                    lastReleaseRate = inst.releaseRate;
                }
            }
            instruments.push_back(inst);
        }
        // Update cross-font residue for next font
        crossFontLoaded = lastLoaded;
        crossFontRangeLo = lastNormalRangeLo;
        crossFontRangeHi = lastNormalRangeHi;
        crossFontRelease = lastReleaseRate;

        // Write counts
        w.Write(static_cast<uint32_t>(drums.size()));
        w.Write(static_cast<uint32_t>(instruments.size()));
        w.Write(static_cast<uint32_t>(sfxEntries.size()));

        // Write drums
        for (auto& [releaseRate, pan, loaded, tuning, env, sampleRef] : drums) {
            w.Write(releaseRate);
            w.Write(pan);
            w.Write(loaded);
            writeEnvData(w, env);
            w.Write(static_cast<uint8_t>(sampleRef.empty() ? 0 : 1));
            w.Write(sampleRef);
            w.Write(tuning);
        }

        // Write instruments
        for (auto& inst : instruments) {
            w.Write(static_cast<uint8_t>(inst.isValid ? 1 : 0));
            w.Write(inst.loaded);
            w.Write(inst.normalRangeLo);
            w.Write(inst.normalRangeHi);
            w.Write(inst.releaseRate);
            writeEnvData(w, inst.env);

            if (inst.isValid) {
                // Low notes
                if (audioBank.ReadU32(inst.lowAddr) != 0) {
                    writeSFE(w, inst.lowAddr, ptr, sampleBankId);
                } else {
                    w.Write(static_cast<uint8_t>(0));
                }
                // Normal notes
                if (audioBank.ReadU32(inst.normalAddr) != 0) {
                    writeSFE(w, inst.normalAddr, ptr, sampleBankId);
                } else {
                    w.Write(static_cast<uint8_t>(0));
                }
                // High notes (ZAudio.cpp:296-297: only if ptr!=0 AND normalRangeHi!=0x7F)
                if (audioBank.ReadU32(inst.highAddr) != 0 && inst.normalRangeHi != 0x7F) {
                    writeSFE(w, inst.highAddr, ptr, sampleBankId);
                } else {
                    w.Write(static_cast<uint8_t>(0));
                }
            } else {
                // Invalid instrument: write 3 null SFEs
                w.Write(static_cast<uint8_t>(0));
                w.Write(static_cast<uint8_t>(0));
                w.Write(static_cast<uint8_t>(0));
            }
        }

        // Write SFX
        for (auto& sfx : sfxEntries) {
            if (sfx.exists) {
                w.Write(static_cast<uint8_t>(1));
                w.Write(static_cast<uint8_t>(1)); // padding
                w.Write(sfx.sampleRef);
                w.Write(sfx.tuning);
            } else {
                w.Write(static_cast<uint8_t>(0));
            }
        }

        // Register companion file
        std::string fontName;
        if (fontNames.count(fi)) {
            fontName = fontNames[fi];
        } else {
            fontName = std::to_string(fi) + "_Font";
        }

        std::stringstream ss;
        w.Finish(ss);
        std::string str = ss.str();
        Companion::Instance->RegisterCompanionFile(
            "fonts/" + fontName, std::vector<char>(str.begin(), str.end()));
    }

    SPDLOG_INFO("OoTAudioFactory: wrote {} font companion files", fontTable.size());
}

ExportResult OoTAudioBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto audio = std::static_pointer_cast<OoTAudioData>(raw);
    write.write(audio->mMainEntry.data(), audio->mMainEntry.size());
    return std::nullopt;
}

} // namespace OoT

#endif
