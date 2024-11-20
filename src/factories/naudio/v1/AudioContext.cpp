#include "AudioContext.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

std::unordered_map<AudioTableType, std::unordered_map<uint32_t, AudioTableEntry>> AudioContext::tables;
std::unordered_map<AudioTableType, std::vector<uint8_t>> AudioContext::data;
std::unordered_map<AudioTableType, std::shared_ptr<AudioTableData>> AudioContext::tableData;

std::optional<std::shared_ptr<IParsedData>> AudioContextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto seq = GetSafeNode<YAML::Node>(node, "audio_seq");
    auto seqSize = GetSafeNode<uint32_t>(seq, "size");
    auto seqOffset = GetSafeNode<uint32_t>(seq, "offset");

    auto bank = GetSafeNode<YAML::Node>(node, "audio_bank");
    auto bankSize = GetSafeNode<uint32_t>(bank, "size");
    auto bankOffset = GetSafeNode<uint32_t>(bank, "offset");

    auto table = GetSafeNode<YAML::Node>(node, "audio_table");
    auto tableSize = GetSafeNode<uint32_t>(table, "size");
    auto tableOffset = GetSafeNode<uint32_t>(table, "offset");

    AudioContext::data[AudioTableType::SEQ_TABLE] = std::vector<uint8_t>(buffer.begin() + seqOffset, buffer.begin() + seqOffset + seqSize);
    AudioContext::data[AudioTableType::FONT_TABLE] = std::vector<uint8_t>(buffer.begin() + bankOffset, buffer.begin() + bankOffset + bankSize);
    AudioContext::data[AudioTableType::SAMPLE_TABLE] = std::vector<uint8_t>(buffer.begin() + tableOffset, buffer.begin() + tableOffset + tableSize);

    SPDLOG_INFO("Sequence Table 0x{:X}", seqOffset);
    SPDLOG_INFO("Sound Font Table 0x{:X}", bankOffset);
    SPDLOG_INFO("Sample Bank Table 0x{:X}", tableOffset);

    return std::nullopt;
}

LUS::BinaryReader AudioContext::MakeReader(AudioTableType type, uint32_t offset) {
    auto entry = AudioContext::data[type];

    LUS::BinaryReader reader(entry.data(), entry.size());
    reader.SetEndianness(Torch::Endianness::Big);
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    return reader;
}

TunedSample AudioContext::LoadTunedSample(LUS::BinaryReader& reader, uint32_t parent, uint32_t sampleBankId) {
    auto sampleAddr = reader.ReadUInt32();
    auto tuning = reader.ReadFloat();

    if(sampleAddr == 0){
        if(tuning != 0.0f){
            throw std::runtime_error("The provided tuned sample is invalid");
        }
        return { 0, 0, 0.0f };
    }

    YAML::Node node;
    node["type"] = "NAUDIO:V1:SAMPLE";
    node["parent"] = parent;
    node["offset"] = parent + sampleAddr;
    node["sampleBankId"] = sampleBankId;
    Companion::Instance->AddAsset(node);

    return { parent + sampleAddr, sampleBankId, tuning };
}

uint64_t AudioContext::GetPathByAddr(uint32_t addr) {
    if(addr == 0){
        return 0;
    }

    auto dec = Companion::Instance->GetNodeByAddr(addr);
    if (dec.has_value()) {
        std::string path = std::get<0>(dec.value());
        uint64_t hash = CRC64(path.c_str());
        SPDLOG_INFO("Found path of 0x{:X} {}", addr, path);
        return hash;
    } else {
        throw std::runtime_error("Failed to find node by addr");
    }
}