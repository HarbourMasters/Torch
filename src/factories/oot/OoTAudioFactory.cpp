#ifdef OOT_SUPPORT

#include "OoTAudioFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <map>
#include <iomanip>

namespace OoT {

// Safe big-endian random-access reader over audio bank data.
// Bounds-checks each read and returns 0 on out-of-range (avoids raw pointer arithmetic).
class SafeAudioBankReader {
public:
    SafeAudioBankReader(const std::vector<uint8_t>& data)
        : mData(data), mReader((char*)data.data(), data.size()) {
        mReader.SetEndianness(Torch::Endianness::Big);
    }

    uint32_t ReadU32(uint32_t offset) {
        if (offset + 4 > mData.size()) return 0;
        mReader.Seek(offset, LUS::SeekOffsetType::Start);
        return mReader.ReadUInt32();
    }

    int16_t ReadS16(uint32_t offset) {
        if (offset + 2 > mData.size()) return 0;
        mReader.Seek(offset, LUS::SeekOffsetType::Start);
        return mReader.ReadInt16();
    }

    float ReadFloat(uint32_t offset) {
        if (offset + 4 > mData.size()) return 0.0f;
        mReader.Seek(offset, LUS::SeekOffsetType::Start);
        return mReader.ReadFloat();
    }

private:
    const std::vector<uint8_t>& mData;
    LUS::BinaryReader mReader;
};

// Audio table entry (16 bytes each in ROM)
struct AudioTableEntry {
    uint32_t ptr;
    uint32_t size;
    uint8_t medium;
    uint8_t cachePolicy;
    int16_t data1;
    int16_t data2;
    int16_t data3;
};

// Parse an audio table header from decompressed code segment
static std::vector<AudioTableEntry> ParseAudioTable(const uint8_t* codeData, uint32_t tableOffset) {
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
static std::vector<std::vector<uint8_t>> ParseSequenceFontTable(
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

std::optional<std::shared_ptr<IParsedData>> OoTAudioFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto data = std::make_shared<OoTAudioData>();

    // Build the main audio entry: just a 64-byte OAUD header with version 2
    {
        LUS::BinaryWriter w;
        BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudio, 2);
        std::stringstream ss;
        w.Finish(ss);
        std::string str = ss.str();
        data->mMainEntry = std::vector<char>(str.begin(), str.end());
    }

    // Read table offsets from YAML
    auto soundFontTableOff = GetSafeNode<uint32_t>(node, "sound_font_table_offset");
    auto sequenceTableOff = GetSafeNode<uint32_t>(node, "sequence_table_offset");
    auto sampleBankTableOff = GetSafeNode<uint32_t>(node, "sample_bank_table_offset");
    auto seqFontTableOff = GetSafeNode<uint32_t>(node, "sequence_font_table_offset");

    // Decompress the code segment (segment 128)
    YAML::Node codeNode;
    codeNode["offset"] = node["offset"].as<uint32_t>();
    auto codeDecoded = Decompressor::AutoDecode(codeNode, buffer, 0x200000);
    const uint8_t* codeData = codeDecoded.segment.data;
    size_t codeSize = codeDecoded.segment.size;

    SPDLOG_INFO("OoTAudioFactory: code segment {} bytes", codeSize);

    // Parse audio tables from code segment
    auto seqTable = ParseAudioTable(codeData, sequenceTableOff);
    auto fontTable = ParseAudioTable(codeData, soundFontTableOff);
    auto sampleBankTable = ParseAudioTable(codeData, sampleBankTableOff);
    auto seqFontMap = ParseSequenceFontTable(codeData, seqFontTableOff, seqTable.size());

    SPDLOG_INFO("OoTAudioFactory: {} sequences, {} fonts, {} sample banks",
                seqTable.size(), fontTable.size(), sampleBankTable.size());

    // Get Audioseq ROM data (segment 2, uncompressed)
    auto audioseqSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(2);
    if (!audioseqSeg.has_value()) {
        SPDLOG_ERROR("OoTAudioFactory: Audioseq segment not found");
        return data;
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

    // Step 3: Extract sequences
    for (uint32_t i = 0; i < seqTable.size(); i++) {
        auto& entry = seqTable[i];
        // When size==0, ptr is an index to another sequence (alias/indirection)
        uint32_t seqIdx = i;
        if (entry.size == 0) {
            seqIdx = entry.ptr;
            if (seqIdx >= seqTable.size()) {
                SPDLOG_WARN("OoTAudioFactory: sequence {} alias index {} out of range", i, seqIdx);
                continue;
            }
        }
        auto& srcEntry = seqTable[seqIdx];

        // Sequence name
        std::string seqName;
        if (i < seqNames.size()) {
            seqName = seqNames[i];
        } else {
            std::ostringstream ss;
            ss << std::setfill('0') << std::setw(3) << i << "_Sequence";
            seqName = ss.str();
        }

        // Read raw sequence data from Audioseq (using srcEntry for aliased sequences)
        uint32_t seqDataOff = audioseqOff + srcEntry.ptr;
        if (seqDataOff + srcEntry.size > buffer.size()) {
            SPDLOG_WARN("OoTAudioFactory: sequence {} out of bounds", seqName);
            continue;
        }

        // Build OSEQ companion file
        LUS::BinaryWriter w;
        BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudioSequence, 2);

        // Sequence data (from srcEntry for aliased sequences)
        w.Write(static_cast<uint32_t>(srcEntry.size));
        w.Write((char*)(buffer.data() + seqDataOff), srcEntry.size);

        // Metadata
        w.Write(static_cast<uint8_t>(i));           // sequence index
        w.Write(entry.medium);                       // medium
        w.Write(entry.cachePolicy);                  // cachePolicy

        // Font indices
        auto& fonts = (i < seqFontMap.size()) ? seqFontMap[i] : seqFontMap[0];
        w.Write(static_cast<uint32_t>(fonts.size()));
        for (auto f : fonts) {
            w.Write(f);
        }

        std::stringstream ss;
        w.Finish(ss);
        std::string str = ss.str();
        std::string companionName = "sequences/" + seqName;
        Companion::Instance->RegisterCompanionFile(
            companionName, std::vector<char>(str.begin(), str.end()));
    }

    SPDLOG_INFO("OoTAudioFactory: wrote {} sequence companion files", seqTable.size());

    // Step 4: Extract samples
    // Load Audiobank and Audiotable as vectors for safe BinaryReader access
    auto audiobankSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(1);
    auto audiotableSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(3);
    if (!audiobankSeg.has_value() || !audiotableSeg.has_value()) {
        SPDLOG_ERROR("OoTAudioFactory: Audiobank/Audiotable segments not found");
        return data;
    }

    // Audiobank and Audiotable are uncompressed — read directly from ROM
    // Compute sizes from DMA table (virt_end - virt_start)
    // For safety, use generous max sizes
    uint32_t bankOff = audiobankSeg.value();
    uint32_t tableOff = audiotableSeg.value();
    uint32_t bankSize = std::min((uint32_t)0x40000, (uint32_t)(buffer.size() - bankOff));
    uint32_t tableSize = std::min((uint32_t)0x500000, (uint32_t)(buffer.size() - tableOff));

    std::vector<uint8_t> audioBankData(buffer.begin() + bankOff, buffer.begin() + bankOff + bankSize);
    SafeAudioBankReader audioBank(audioBankData);
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

    // Sample info struct
    struct SampleInfo {
        uint8_t codec, medium, unk_bit26, unk_bit25;
        uint32_t dataSize, dataOffset;
        int32_t loopStart, loopEnd, loopCount;
        std::vector<int16_t> loopStates;
        int32_t bookOrder, bookNpredictors;
        std::vector<int16_t> books;
        std::string name;
    };
    std::map<uint32_t, SampleInfo> sampleMap;

    // Parse a sample entry at sampleAddr in audioBank
    auto parseSample = [&](int bankIndex, uint32_t sampleAddr, uint32_t baseOffset) {
        if (sampleAddr + 16 > audioBankData.size()) return;

        uint32_t dataRelPtr = audioBank.ReadU32(sampleAddr + 4);
        uint32_t sampleDataOffset = dataRelPtr + sampleBankTable[bankIndex].ptr;
        if (sampleMap.count(sampleDataOffset)) return;

        SampleInfo s;
        uint32_t origField = audioBank.ReadU32(sampleAddr);
        s.codec = (origField >> 28) & 0x0F;
        s.medium = (origField >> 24) & 0x03;
        s.unk_bit26 = (origField >> 22) & 0x01;
        s.unk_bit25 = (origField >> 21) & 0x01;
        s.dataSize = origField & 0x00FFFFFF;
        s.dataOffset = sampleDataOffset;

        uint32_t loopAddr = audioBank.ReadU32(sampleAddr + 8) + baseOffset;
        uint32_t bookAddr = audioBank.ReadU32(sampleAddr + 12) + baseOffset;

        if (loopAddr + 12 <= audioBankData.size()) {
            s.loopStart = (int32_t)audioBank.ReadU32(loopAddr);
            s.loopEnd = (int32_t)audioBank.ReadU32(loopAddr + 4);
            s.loopCount = (int32_t)audioBank.ReadU32(loopAddr + 8);

            if (s.loopCount != 0 && loopAddr + 48 <= audioBankData.size()) {
                for (int i = 0; i < 16; i++) {
                    s.loopStates.push_back(audioBank.ReadS16(loopAddr + 16 + i * 2));
                }
            }
        }

        if (bookAddr + 8 <= audioBankData.size()) {
            s.bookOrder = (int32_t)audioBank.ReadU32(bookAddr);
            s.bookNpredictors = (int32_t)audioBank.ReadU32(bookAddr + 4);
            int numBooks = s.bookOrder * s.bookNpredictors * 8;
            if (bookAddr + 8 + numBooks * 2 <= audioBankData.size()) {
                for (int i = 0; i < numBooks; i++) {
                    s.books.push_back(audioBank.ReadS16(bookAddr + 8 + i * 2));
                }
            }
        }

        // Resolve name from YAML (use absolute offset as key, matching ZAPDTR behavior)
        if (sampleNames.count(bankIndex) && sampleNames[bankIndex].count((int)sampleDataOffset)) {
            s.name = sampleNames[bankIndex][(int)sampleDataOffset];
        } else {
            std::ostringstream ss;
            ss << "sample_" << bankIndex << "_" << std::setfill('0') << std::setw(8)
               << std::hex << std::uppercase << sampleDataOffset;
            s.name = ss.str();
        }

        sampleMap[sampleDataOffset] = s;
    };

    // Parse sample from a SoundFontEntry (pointer at soundFontAddr)
    auto parseSFESample = [&](int bankIndex, uint32_t sfeAddr, uint32_t baseOffset) {
        if (sfeAddr + 4 > audioBankData.size()) return;
        uint32_t samplePtr = audioBank.ReadU32(sfeAddr);
        if (samplePtr != 0) {
            uint32_t sampleAddr = samplePtr + baseOffset;
            parseSample(bankIndex, sampleAddr, baseOffset);
        }
    };

    // Iterate all fonts to discover samples
    for (uint32_t fi = 0; fi < fontTable.size(); fi++) {
        auto& fe = fontTable[fi];
        uint32_t ptr = fe.ptr;
        int sampleBankId = (fe.data1 >> 8) & 0xFF;
        int numInstruments = (fe.data2 >> 8) & 0xFF;
        int numDrums = fe.data2 & 0xFF;
        int numSfx = fe.data3;

        if (ptr + 8 > audioBankData.size()) continue;

        // Drums
        uint32_t drumListAddr = audioBank.ReadU32(ptr) + ptr;
        for (int i = 0; i < numDrums; i++) {
            if (drumListAddr + (i + 1) * 4 > audioBankData.size()) break;
            uint32_t drumPtr = audioBank.ReadU32(drumListAddr + i * 4);
            if (drumPtr != 0) {
                drumPtr += ptr;
                if (drumPtr + 8 > audioBankData.size()) continue;
                // Drum struct: byte0=releaseRate, byte1=pan, byte2=loaded, byte3=pad
                // bytes 4-7 = pointer to sample entry (relative to ptr)
                uint32_t sampleEntryPtr = audioBank.ReadU32(drumPtr + 4) + ptr;
                parseSample(sampleBankId, sampleEntryPtr, ptr);
            }
        }

        // SFX
        uint32_t sfxListAddr = audioBank.ReadU32(ptr + 4) + ptr;
        for (int i = 0; i < numSfx; i++) {
            if (sfxListAddr + (i + 1) * 8 > audioBankData.size()) break;
            parseSFESample(sampleBankId, sfxListAddr + i * 8, ptr);
        }

        // Instruments
        for (int i = 0; i < numInstruments; i++) {
            if (ptr + 8 + (i + 1) * 4 > audioBankData.size()) break;
            uint32_t instPtr = audioBank.ReadU32(ptr + 8 + i * 4);
            if (instPtr != 0) {
                instPtr += ptr;
                if (instPtr + 28 > audioBankData.size()) continue;
                // Instrument: bytes 0-7 = metadata, then 3 SoundFontEntries at +8, +16, +24
                parseSFESample(sampleBankId, instPtr + 8, ptr);
                parseSFESample(sampleBankId, instPtr + 16, ptr);
                parseSFESample(sampleBankId, instPtr + 24, ptr);
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

    // Step 5: Extract fonts (OSFT companion files)
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

    return data;
}

ExportResult OoTAudioBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto audio = std::static_pointer_cast<OoTAudioData>(raw);
    write.write(audio->mMainEntry.data(), audio->mMainEntry.size());
    return std::nullopt;
}

} // namespace OoT

#endif
