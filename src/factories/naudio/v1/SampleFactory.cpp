#include "SampleFactory.h"
#include "AudioConverter.h"
#include "Companion.h"
#include <factories/naudio/v0/AIFCDecode.h>

ExportResult NSampleHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Sample " << symbol << ";\n";

    return std::nullopt;
}

ExportResult NSampleCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    return std::nullopt;
}

ExportResult NSampleBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<NSampleData>(raw);

    WriteHeader(writer, Torch::ResourceType::Sample, 1);
    writer.Write(data->codec);
    writer.Write(data->medium);
    writer.Write(data->unk);
    writer.Write(data->size);

    writer.Write(AudioContext::GetPathByAddr(data->loop));
    writer.Write(AudioContext::GetPathByAddr(data->book));

    auto entry = AudioContext::tableData[AudioTableType::SAMPLE_TABLE]->entries[data->sampleBankId];
    auto buffer = AudioContext::data[AudioTableType::SAMPLE_TABLE];

    writer.Write((char*) buffer.data() + entry.addr + data->sampleAddr, data->size);

    writer.Finish(write);
    return std::nullopt;
}

ExportResult NSampleModdingExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto aiff = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<NSampleData>(raw);
    *replacement += ".aiff";

    auto aifc = LUS::BinaryWriter();
    AudioConverter::SampleToAIFC(data.get(), aifc);
    auto cnv = aifc.ToVector();

    if(!cnv.empty()){
        write_aiff(cnv, aiff);
        aiff.Finish(write);
    }
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> NSampleFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto parent = GetSafeNode<uint32_t>(node, "parent");
    auto tuning = GetSafeNode<float>(node, "tuning", 1.0f);
    auto sampleBankId = GetSafeNode<uint32_t>(node, "sampleBankId");

    auto entry = AudioContext::tables[AudioTableType::FONT_TABLE].at(parent);
    auto reader = AudioContext::MakeReader(AudioTableType::FONT_TABLE, offset);

    auto sample = std::make_shared<NSampleData>();

    uint32_t flags = reader.ReadUInt32();
    uint32_t addr = reader.ReadUInt32();

    sample->codec = (flags >> 28) & 0x0F;
    sample->medium = (flags >> 24) & 0x03;
    sample->unk = (flags >> 22) & 0x01;
    sample->size = flags;

    auto loopAddr = entry.addr + reader.ReadUInt32();
    YAML::Node loop;
    loop["type"] = "NAUDIO:V1:ADPCM_LOOP";
    loop["offset"] = loopAddr;
    sample->loop = loopAddr;
    Companion::Instance->AddAsset(loop);

    auto bookAddr = entry.addr + reader.ReadUInt32();
    YAML::Node book;
    book["type"] = "NAUDIO:V1:ADPCM_BOOK";
    book["offset"] = bookAddr;
    sample->book = bookAddr;
    Companion::Instance->AddAsset(book);

    sample->sampleAddr = addr;
    sample->tuning = tuning;
    sample->sampleBankId = sampleBankId;

    return sample;
}
