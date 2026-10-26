#include "AudioContext.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

std::unordered_map<AudioTableType, TableEntry> AudioContext::tables;
uint32_t AudioContext::sessionFrequency = 32000;
uint32_t AudioContext::numBuffers = 2;
NAudioDrivers AudioContext::driver = NAudioDrivers::UNKNOWN;
std::unordered_map<uint64_t, std::string> AudioContext::sampleDedup;
std::unordered_map<uint32_t, std::string> AudioContext::sampleAddrRemap;

struct AudioWindow {
    uint32_t offset;
    uint32_t size;
};

static void SetTableWindow(std::vector<uint8_t>& buffer, AudioTableType type, AudioWindow window, bool translate) {
    const uint32_t start = translate ? Decompressor::TranslateAddr(window.offset) : window.offset;

    if ((size_t)start + window.size > buffer.size()) {
        throw std::runtime_error("NAudio table window is outside the input");
    }

    AudioContext::tables[type].buffer = std::vector<uint8_t>(buffer.begin() + start, buffer.begin() + start + window.size);
    AudioContext::tables[type].offset = window.offset;
}

static AudioWindow ReadWindow(YAML::Node& node, const char* key) {
    auto window = GetSafeNode<YAML::Node>(node, key);
    return { GetSafeNode<uint32_t>(window, "offset"), GetSafeNode<uint32_t>(window, "size") };
}

static std::array<AudioWindow, 3> ReadNeosDataHeader(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto header = GetSafeNode<uint32_t>(node, "audio_data");
    const auto romBase = GetSafeNode<uint32_t>(node, "audiorom");
    const uint32_t start = Decompressor::TranslateAddr(header);

    LUS::BinaryReader reader((char*)buffer.data() + start, 0x10 + 3 * 0x10);
    reader.SetEndianness(Torch::Endianness::Big);

    if (reader.ReadInt16() < 3) {
        throw std::runtime_error("Neos data header has fewer than three entries");
    }
    reader.Seek(0x10, LUS::SeekOffsetType::Start);

    std::array<AudioWindow, 3> windows;
    for (auto& window : windows) {
        window.offset = romBase + reader.ReadUInt32();
        window.size = reader.ReadUInt32();
        reader.Read(8);
    }
    return windows;
}

std::optional<std::shared_ptr<IParsedData>> AudioContextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    AudioContext::tables.clear();
    AudioContext::sampleDedup.clear();
    AudioContext::sampleAddrRemap.clear();

    auto driver = GetSafeNode<std::string>(node, "driver");

    if (driver == "SF64") {
        AudioContext::driver = NAudioDrivers::SF64;
    } else if (driver == "FZEROX") {
        AudioContext::driver = NAudioDrivers::FZEROX;
    } else if (driver == "NEOS") {
        AudioContext::driver = NAudioDrivers::NEOS;
    } else {
        throw std::runtime_error("Unknown NAudio driver");
    }

    AudioContext::sessionFrequency = GetSafeNode<uint32_t>(node, "frequency", 32000);
    AudioContext::numBuffers = GetSafeNode<uint32_t>(node, "buffers", 2);

    AudioWindow seq, bank, table;
    if (AudioContext::IsNeos() && node["audio_data"]) {
        auto windows = ReadNeosDataHeader(buffer, node);
        seq = windows[0];
        bank = windows[1];
        table = windows[2];
    } else {
        seq = ReadWindow(node, "audio_seq");
        bank = ReadWindow(node, "audio_bank");
        table = ReadWindow(node, "audio_table");
    }

    const bool translate = AudioContext::IsNeos();
    SetTableWindow(buffer, AudioTableType::SEQ_TABLE, seq, translate);
    SetTableWindow(buffer, AudioTableType::FONT_TABLE, bank, translate);
    SetTableWindow(buffer, AudioTableType::SAMPLE_TABLE, table, translate);

    SPDLOG_INFO("Sequence Table 0x{:X}", seq.offset);
    SPDLOG_INFO("Sound Font Table 0x{:X}", bank.offset);
    SPDLOG_INFO("Sample Bank Table 0x{:X}", table.offset);

    node["no_export"] = true;
    return std::make_shared<IParsedData>();
}

LUS::BinaryReader AudioContext::MakeReader(AudioTableType type, uint32_t offset) {
    auto& entry = AudioContext::tables[type].buffer;

    LUS::BinaryReader reader(entry.data(), entry.size());
    reader.SetEndianness(Torch::Endianness::Big);
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    return reader;
}

TunedSample AudioContext::LoadTunedSample(LUS::BinaryReader& reader, uint32_t parent, uint32_t sampleBankId) {
    return LoadTunedSample(reader, parent, SampleBankIds{ sampleBankId, 0xFF });
}

SampleBankIds AudioContext::GetSampleBankIds(YAML::Node& node) {
    return { GetSafeNode<uint32_t>(node, "sampleBankId"), GetSafeNode<uint32_t>(node, "sampleBankId2", 0xFF) };
}

uint32_t AudioContext::ResolveTableIndex(AudioTableType type, uint32_t index) {
    auto& info = tables[type].info;
    if (info == nullptr || index >= info->entries.size()) {
        return index;
    }
    const auto& entry = info->entries[index];
    return entry.size == 0 ? entry.addr : index;
}

TunedSample AudioContext::LoadTunedSample(LUS::BinaryReader& reader, uint32_t parent, SampleBankIds banks) {
    const uint32_t sampleBankId = banks.bank1;
    auto sampleAddr = reader.ReadUInt32();
    auto tuning = reader.ReadFloat();

    if (sampleAddr == 0) {
        if (tuning != 0.0f) {
            throw std::runtime_error("The provided tuned sample is invalid");
        }
        return { 0, 0, 0.0f };
    }

    YAML::Node node;
    node["type"] = "NAUDIO:V1:SAMPLE";
    node["parent"] = parent;
    node["offset"] = parent + sampleAddr;
    node["tuning"] = tuning;
    node["sampleBankId"] = sampleBankId;
    if (banks.bank2 != 0xFF) {
        node["sampleBankId2"] = banks.bank2;
    }
    Companion::Instance->AddAsset(node);

    return { parent + sampleAddr, sampleBankId, tuning };
}

uint64_t AudioContext::GetPathByAddr(uint32_t addr) {
    if (addr == 0) {
        return 0;
    }

    // If this ROM address belongs to a duplicate SampleData struct, return the
    // canonical's hash so instruments/drums reference the canonical entry directly.
    auto remapIt = sampleAddrRemap.find(addr);
    if (remapIt != sampleAddrRemap.end()) {
        SPDLOG_INFO("Remapping duplicate 0x{:X} to canonical {}", addr, remapIt->second);
        return CRC64(remapIt->second.c_str());
    }

    auto dec = Companion::Instance->GetNodeByAddr(addr);
    if (dec.has_value()) {
        std::string path = std::get<0>(dec.value());
        uint64_t hash = CRC64(path.c_str());
        SPDLOG_INFO("Found path of 0x{:X} {}", addr, path);
        return hash;
    } else {
        SPDLOG_INFO("Failed to find path for 0x{:X}", addr);
        throw std::runtime_error("Failed to find node by addr");
    }
}

const char* AudioContext::GetMediumStr(uint8_t medium) {
    switch (medium) {
        case 0:
            return "Ram";
        case 1:
            return "Unk";
        case 2:
            return "Cart";
        case 3:
            return "Disk";
        case 5:
            return "RamUnloaded";
        default:
            return "ERROR";
    }
}

const char* AudioContext::GetCachePolicyStr(uint8_t policy) {
    switch (policy) {
        case 0:
            return "Temporary";
        case 1:
            return "Persistent";
        case 2:
            return "Either";
        case 3:
            return "Permanent";
        default:
            return "ERROR";
    }
}

const char* AudioContext::GetCodecStr(uint8_t codec) {
    switch (codec) {
        case 0:
            return "ADPCM";
        case 1:
            return "S8";
        case 2:
            return "S16MEM";
        case 3:
            return "ADPCMSMALL";
        case 4:
            return "REVERB";
        case 5:
            return "S16";
        case 6:
            return "UNK6";
        case 7:
            return "UNK7";
        default:
            return "ERROR";
    }
}