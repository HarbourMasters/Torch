#include "SequenceFactory.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "AudioContext.h"

ExportResult NSequenceHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern u8 " << symbol << "[];\n";

    return std::nullopt;
}

ExportResult NSequenceCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[] = {\n" << tab_t;

    for (int i = 0; i < data.size(); i++) {
        if ((i % 15 == 0) && i != 0) {
            write << "\n" << tab_t;
        }

        write << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << ", ";
    }
    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// size: 0x" << std::hex << std::uppercase << data.size() << "\n";
    }

    return offset + data.size();
}

ExportResult NSequenceModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;
    *replacement += ".m64";
    writer.Write((char*)data.data(), data.size());
    writer.Finish(write);
    return std::nullopt;
}

ExportResult NSequenceBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    WriteHeader(writer, Torch::ResourceType::Sequence, 2);

    // seqDataSize + raw bytes (matches Shipwright V2 binary layout)
    writer.Write((uint32_t)data.size());
    writer.Write((char*)data.data(), data.size());

    // Sequence metadata from the audio table entry
    uint8_t medium = 0;
    uint8_t cachePolicy = 0;
    uint8_t seqNumber = 0;

    auto& seqTable = AudioContext::tables[AudioTableType::SEQ_TABLE];
    auto it = seqTable.entries.find(offset - seqTable.offset);
    if (it != seqTable.entries.end()) {
        medium = (uint8_t)it->second.medium;
        cachePolicy = (uint8_t)it->second.cachePolicy;
    }

    // Derive seqNumber from the entry's position in the table
    uint32_t idx = 0;
    for (auto& [entryOffset, entry] : seqTable.entries) {
        if (entryOffset == (offset - seqTable.offset))
            break;
        idx++;
    }
    seqNumber = (uint8_t)idx;

    writer.Write(seqNumber);
    writer.Write(medium);
    writer.Write(cachePolicy);

    // numFonts and fonts[] — SF64 uses a separate gSeqFontTable blob at runtime,
    // so we write numFonts=0 here; the font loading path reads from that blob.
    writer.Write((uint32_t)0);
    for (int i = 0; i < 16; i++) {
        writer.Write((uint8_t)0);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> NSequenceFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto size = GetSafeNode<size_t>(node, "size");
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, size);
    return std::make_shared<RawBuffer>(segment.data, segment.size);
}

std::optional<std::shared_ptr<IParsedData>> NSequenceFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                            YAML::Node& node) {
    return std::make_shared<RawBuffer>(buffer.data(), buffer.size());
}
