#ifdef OOT_SUPPORT

#include "OoTAudioFactory.h"
#include "OoTAudioSequenceWriter.h"
#include "OoTAudioSampleWriter.h"
#include "OoTAudioFontWriter.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <sstream>

namespace OoT {

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

std::vector<std::vector<uint8_t>> OoTAudioFactory::ParseSequenceFontTable(
    const uint8_t* codeData, uint32_t tableOffset, uint32_t numSequences) {
    std::vector<std::vector<uint8_t>> result;
    result.reserve(numSequences);

    LUS::BinaryReader reader((char*)(codeData + tableOffset), 0x10000);
    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<uint16_t> offsets;
    for (uint32_t i = 0; i < numSequences; i++) {
        offsets.push_back(reader.ReadUInt16());
    }

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

std::optional<SafeAudioBankReader> OoTAudioFactory::LoadAudioBank(std::vector<uint8_t>& buffer) {
    auto audiobankSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(1);
    if (!audiobankSeg.has_value()) {
        SPDLOG_ERROR("OoTAudioFactory: Audiobank segment not found");
        return std::nullopt;
    }
    uint32_t bankOff = audiobankSeg.value();
    uint32_t bankSize = std::min((uint32_t)0x40000, (uint32_t)(buffer.size() - bankOff));
    return SafeAudioBankReader(std::vector<uint8_t>(buffer.begin() + bankOff, buffer.begin() + bankOff + bankSize));
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

    AudioSequenceWriter seqWriter;
    if (!seqWriter.Extract(buffer, node, seqTable, seqFontMap)) {
        return data;
    }

    auto audioBank = LoadAudioBank(buffer);
    if (!audioBank.has_value()) {
        return data;
    }

    std::map<uint32_t, SampleInfo> sampleMap;
    AudioSampleWriter sampleWriter;
    if (!sampleWriter.Extract(buffer, node, audioBank.value(), fontTable, sampleBankTable, sampleMap)) {
        return data;
    }

    AudioFontWriter fontWriter;
    fontWriter.Extract(node, audioBank.value(), fontTable, sampleBankTable, sampleMap);

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
