#ifdef OOT_SUPPORT

#include "OoTAudioFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <map>
#include <iomanip>

namespace OoT {

// Safe BE read helpers using BinaryReader (no raw pointer arithmetic)
static uint32_t readBE32(const std::vector<uint8_t>& data, uint32_t offset) {
    if (offset + 4 > data.size()) return 0;
    LUS::BinaryReader r((char*)data.data() + offset, 4);
    r.SetEndianness(Torch::Endianness::Big);
    return r.ReadUInt32();
}

static int16_t readBE16(const std::vector<uint8_t>& data, uint32_t offset) {
    if (offset + 2 > data.size()) return 0;
    LUS::BinaryReader r((char*)data.data() + offset, 2);
    r.SetEndianness(Torch::Endianness::Big);
    return r.ReadInt16();
}

static float readBEFloat(const std::vector<uint8_t>& data, uint32_t offset) {
    if (offset + 4 > data.size()) return 0.0f;
    LUS::BinaryReader r((char*)data.data() + offset, 4);
    r.SetEndianness(Torch::Endianness::Big);
    return r.ReadFloat();
}

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

    std::vector<uint8_t> audioBank(buffer.begin() + bankOff, buffer.begin() + bankOff + bankSize);
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
        if (sampleAddr + 16 > audioBank.size()) return;

        uint32_t dataRelPtr = readBE32(audioBank, sampleAddr + 4);
        uint32_t sampleDataOffset = dataRelPtr + sampleBankTable[bankIndex].ptr;
        if (sampleMap.count(sampleDataOffset)) return;

        SampleInfo s;
        uint32_t origField = readBE32(audioBank, sampleAddr);
        s.codec = (origField >> 28) & 0x0F;
        s.medium = (origField >> 24) & 0x03;
        s.unk_bit26 = (origField >> 22) & 0x01;
        s.unk_bit25 = (origField >> 21) & 0x01;
        s.dataSize = origField & 0x00FFFFFF;
        s.dataOffset = sampleDataOffset;

        uint32_t loopAddr = readBE32(audioBank, sampleAddr + 8) + baseOffset;
        uint32_t bookAddr = readBE32(audioBank, sampleAddr + 12) + baseOffset;

        if (loopAddr + 12 <= audioBank.size()) {
            s.loopStart = (int32_t)readBE32(audioBank, loopAddr);
            s.loopEnd = (int32_t)readBE32(audioBank, loopAddr + 4);
            s.loopCount = (int32_t)readBE32(audioBank, loopAddr + 8);

            if (s.loopCount != 0 && loopAddr + 48 <= audioBank.size()) {
                for (int i = 0; i < 16; i++) {
                    s.loopStates.push_back(readBE16(audioBank, loopAddr + 16 + i * 2));
                }
            }
        }

        if (bookAddr + 8 <= audioBank.size()) {
            s.bookOrder = (int32_t)readBE32(audioBank, bookAddr);
            s.bookNpredictors = (int32_t)readBE32(audioBank, bookAddr + 4);
            int numBooks = s.bookOrder * s.bookNpredictors * 8;
            if (bookAddr + 8 + numBooks * 2 <= audioBank.size()) {
                for (int i = 0; i < numBooks; i++) {
                    s.books.push_back(readBE16(audioBank, bookAddr + 8 + i * 2));
                }
            }
        }

        // Resolve name from YAML
        int sampleRelOff = (int32_t)readBE32(audioBank, sampleAddr + 4);
        if (sampleNames.count(bankIndex) && sampleNames[bankIndex].count(sampleRelOff)) {
            s.name = sampleNames[bankIndex][sampleRelOff];
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
        if (sfeAddr + 4 > audioBank.size()) return;
        uint32_t samplePtr = readBE32(audioBank, sfeAddr);
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

        if (ptr + 8 > audioBank.size()) continue;

        // Drums
        uint32_t drumListAddr = readBE32(audioBank, ptr) + ptr;
        for (int i = 0; i < numDrums; i++) {
            if (drumListAddr + (i + 1) * 4 > audioBank.size()) break;
            uint32_t drumPtr = readBE32(audioBank, drumListAddr + i * 4);
            if (drumPtr != 0) {
                drumPtr += ptr;
                if (drumPtr + 8 > audioBank.size()) continue;
                // Drum struct: byte0=releaseRate, byte1=pan, byte2=loaded, byte3=pad
                // bytes 4-7 = pointer to sample entry (relative to ptr)
                uint32_t sampleEntryPtr = readBE32(audioBank, drumPtr + 4) + ptr;
                parseSample(sampleBankId, sampleEntryPtr, ptr);
            }
        }

        // SFX
        uint32_t sfxListAddr = readBE32(audioBank, ptr + 4) + ptr;
        for (int i = 0; i < numSfx; i++) {
            if (sfxListAddr + (i + 1) * 8 > audioBank.size()) break;
            parseSFESample(sampleBankId, sfxListAddr + i * 8, ptr);
        }

        // Instruments
        for (int i = 0; i < numInstruments; i++) {
            if (ptr + 8 + (i + 1) * 4 > audioBank.size()) break;
            uint32_t instPtr = readBE32(audioBank, ptr + 8 + i * 4);
            if (instPtr != 0) {
                instPtr += ptr;
                if (instPtr + 28 > audioBank.size()) continue;
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
