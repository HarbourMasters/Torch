#include "AudioTableFactory.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include "spdlog/spdlog.h"
#include "AudioContext.h"
#include "Companion.h"
#include <iomanip>

#define INT2(c) std::dec << std::setfill(' ') << c
#define HEX(c) "0x" << std::hex << std::setw(4) << std::setfill('0') << c
#define ADDR(c) "0x" << std::hex << std::setw(5) << std::setfill('0') << c

static const std::unordered_map<std::string, AudioTableType> gTableTypes = {
    { "SOUNDFONT", AudioTableType::FONT_TABLE },
    { "SAMPLE", AudioTableType::SAMPLE_TABLE },
    { "SEQUENCE", AudioTableType::SEQ_TABLE }
};

ExportResult AudioTableHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern AudioTable " << symbol << ";\n";

    return std::nullopt;
}

ExportResult AudioTableCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto table = std::static_pointer_cast<AudioTableData>(raw);

    if (table->type == AudioTableType::FONT_TABLE) {
        write << "#define SOUNDFONT_ENTRY(offset, size, medium, cachePolicy, bank1, bank2, numInst, numDrums) { \\\n";
        write << fourSpaceTab
              << "offset, size, medium, cachePolicy, (((bank1) &0xFF) << 8) | ((bank2) &0xFF),              \\\n";
        write << fourSpaceTab << fourSpaceTab
              << "(((numInst) &0xFF) << 8) | ((numDrums) &0xFF)                                         \\\n}\n\n";
    }

    write << "AudioTable " << symbol << " = {\n";
    write << fourSpaceTab << "{ " << table->entries.size() << ", " << table->medium << ", " << table->addr << " },\n";
    write << fourSpaceTab << "{ \n";

    for (size_t i = 0; i < table->entries.size(); i++) {
        auto& entry = table->entries[i];

        if (table->type == AudioTableType::FONT_TABLE) {
            uint32_t numInstruments = (entry.shortData2 >> 8) & 0xFFu;
            uint32_t numDrums = entry.shortData2 & 0xFFu;
            uint32_t sampleBankId1 = (entry.shortData1 >> 8) & 0xFFu;
            uint32_t sampleBankId2 = entry.shortData1 & 0xFFu;

            write << fourSpaceTab << fourSpaceTab << "SOUNDFONT_ENTRY(" << ADDR(entry.addr) << ", " << HEX(entry.size)
                  << ", " << INT2((uint32_t)entry.medium) << ", " << INT2((uint32_t)entry.cachePolicy) << ", "
                  << INT2(numInstruments) << ", " << INT2(numDrums) << ", " << INT2(sampleBankId1) << ", "
                  << INT2(sampleBankId2) << "),\n";
        } else {
            write << fourSpaceTab << fourSpaceTab << "{ " << ADDR(entry.addr) << ", " << HEX(entry.size) << ", "
                  << (uint32_t)entry.medium << ", " << (uint32_t)entry.cachePolicy << " },\n";
        }
    }

    write << fourSpaceTab << "},\n";
    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// Count: " << table->entries.size() << "\n";
    }

    return offset + 0x10 + (table->entries.size() * 0x10);
}

ExportResult AudioTableBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<AudioTableData>(raw);

    WriteHeader(writer, Torch::ResourceType::AudioTable, 0);
    writer.Write(data->medium);
    writer.Write(data->addr);
    writer.Write((uint32_t)data->entries.size());

    for (auto& entry : data->entries) {
        if (data->type == AudioTableType::FONT_TABLE && entry.crc == 0) {
            throw std::runtime_error("Fuck this thing");
        }
        if (entry.size == 0) {
            auto item = AudioContext::tables[AudioTableType::SEQ_TABLE].info->entries[entry.addr];
            writer.Write(item.crc);
        } else {
            writer.Write(entry.crc);
        }
        writer.Write(entry.size);
        writer.Write(entry.medium);
        writer.Write(entry.cachePolicy);
        writer.Write(entry.shortData1);
        writer.Write(entry.shortData2);
        writer.Write(entry.shortData3);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> AudioTableFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // Parse table entry
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto format = GetSafeNode<std::string>(node, "format");
    std::transform(format.begin(), format.end(), format.begin(), ::toupper);

    if (!Torch::contains(gTableTypes, format)) {
        return std::nullopt;
    }

    auto type = gTableTypes.at(format);
    auto count = reader.ReadInt16();
    auto bmedium = reader.ReadInt16();
    auto baddr = reader.ReadUInt32();
    reader.Read(0x8); // PAD

    SPDLOG_INFO("count: {}", count);
    SPDLOG_INFO("medium: {}", bmedium);
    SPDLOG_INFO("addr: {}", baddr);

    // Before the SoundFont cascade begins, pre-populate sampleDedup with every
    // explicitly-declared NAUDIO:V1:SAMPLE entry so that auto-generated
    // duplicates (same bankId + sampleAddr, different parent) are suppressed
    // and don't shadow the explicit canonical entries.
    if (type == AudioTableType::FONT_TABLE) {
        auto explicitSamples = Companion::Instance->GetNodesByType("NAUDIO:V1:SAMPLE");
        if (explicitSamples.has_value()) {
            auto& fontBuf = AudioContext::tables[AudioTableType::FONT_TABLE].buffer;
            size_t prePopCount = 0;
            size_t skippedCount = 0;
            SPDLOG_INFO("Pre-populating sampleDedup from {} explicit NAUDIO:V1:SAMPLE entries (fontBuf size=0x{:X})",
                        explicitSamples->size(), fontBuf.size());
            for (auto& [path, sampleNode] : explicitSamples.value()) {
                uint32_t sampleOffset = 0, sampleBankId = 0;
                try {
                    sampleOffset = GetSafeNode<uint32_t>(sampleNode, "offset");
                    sampleBankId = GetSafeNode<uint32_t>(sampleNode, "sampleBankId");
                } catch (...) {
                    SPDLOG_WARN("sampleDedup pre-pop: skipping {} — missing offset or sampleBankId", path);
                    skippedCount++;
                    continue;
                }
                if (sampleOffset == 0 || sampleOffset + 8 > fontBuf.size()) {
                    SPDLOG_WARN("sampleDedup pre-pop: skipping {} — offset 0x{:X} out of fontBuf bounds (0x{:X})", path,
                                sampleOffset, fontBuf.size());
                    skippedCount++;
                    continue;
                }
                auto sReader = AudioContext::MakeReader(AudioTableType::FONT_TABLE, sampleOffset);
                sReader.ReadUInt32(); // skip flags
                uint32_t sampleAddr = sReader.ReadUInt32();
                if (sampleAddr == 0) {
                    skippedCount++;
                    continue;
                }
                uint64_t key = ((uint64_t)sampleBankId << 32) | (uint64_t)sampleAddr;
                AudioContext::sampleDedup[key] = path;
                prePopCount++;
            }
            SPDLOG_INFO("sampleDedup pre-pop done: {} registered, {} skipped", prePopCount, skippedCount);
        }
    }

    std::vector<AudioTableEntry> entries;

    for (size_t i = 0; i < count; i++) {
        auto addr = reader.ReadUInt32();
        auto size = reader.ReadUInt32();
        auto medium = reader.ReadInt8();
        auto policy = reader.ReadInt8();
        auto sd1 = reader.ReadInt16();
        auto sd2 = reader.ReadInt16();
        auto sd3 = reader.ReadInt16();
        uint64_t crc = 0;

        auto entry = AudioTableEntry{ addr, size, medium, policy, sd1, sd2, sd3, crc };
        AudioContext::tables[type].entries[addr] = entry;

        switch (type) {
            case AudioTableType::FONT_TABLE: {
                YAML::Node font;
                font["type"] = "NAUDIO:V1:SOUND_FONT";
                font["offset"] = addr;
                font["id"] = (uint32_t)i;
                font["medium"] = (int32_t)medium;
                font["policy"] = (int32_t)policy;
                font["sd1"] = (int32_t)sd1;
                font["sd2"] = (int32_t)sd2;
                font["sd3"] = (int32_t)sd3;

                // On re-parse AddAsset returns the already-registered node.
                auto res = Companion::Instance->AddAsset(font);
                if (res.has_value() && (*res)["vpath"]) {
                    crc = CRC64((*res)["vpath"].as<std::string>().c_str());
                } else if (font["vpath"]) {
                    crc = CRC64(font["vpath"].as<std::string>().c_str());
                }
                break;
            }
            case AudioTableType::SEQ_TABLE: {
                auto parent = AudioContext::tables[AudioTableType::SEQ_TABLE].offset;
                if (size != 0) {
                    YAML::Node seq;
                    seq["type"] = "NAUDIO:V1:SEQUENCE";
                    seq["offset"] = parent + addr;
                    seq["size"] = size;
                    auto res = Companion::Instance->AddAsset(seq);
                    if (res.has_value() && (*res)["vpath"]) {
                        crc = CRC64((*res)["vpath"].as<std::string>().c_str());
                    } else if (seq["vpath"]) {
                        crc = CRC64(seq["vpath"].as<std::string>().c_str());
                    }
                    break;
                }
            }
        }

        entry.crc = crc;
        AudioContext::tables[type].entries[addr].crc = crc;
        entries.push_back(entry);
    }

    auto table = std::make_shared<AudioTableData>(bmedium, offset, type, entries);
    AudioContext::tables[type].info = table;
    return table;
}
