#ifdef OOT_SUPPORT

#include "OoTAudioFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

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
