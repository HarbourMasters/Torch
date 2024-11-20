#include "SoundFontFactory.h"
#include "AudioContext.h"
#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"

ExportResult SoundFontHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern SoundFont " << symbol << ";\n";

    return std::nullopt;
}

ExportResult SoundFontCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    // auto symbol = GetSafeNode(node, "symbol", entryName);
    // auto offset = GetSafeNode<uint32_t>(node, "offset");
    // auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    // if(Companion::Instance->IsOTRMode()){
    //     write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
    //     return std::nullopt;
    // }

    // write << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[] = {\n" << tab_t;

    // for (int i = 0; i < data.size(); i++) {
    //     if ((i % 15 == 0) && i != 0) {
    //         write << "\n" << tab_t;
    //     }

    //     write << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int) data[i] << ", ";
    // }
    // write << "\n};\n";

    // if (Companion::Instance->IsDebug()) {
    //     write << "// size: 0x" << std::hex << std::uppercase << data.size() << "\n";
    // }

    // return offset + data.size();
    return std::nullopt;
}

ExportResult SoundFontBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<SoundFontData>(raw);

    WriteHeader(writer, Torch::ResourceType::SoundFont, 0);
    writer.Write(data->numInstruments);
    writer.Write(data->numDrums);
    writer.Write(data->sampleBankId1);
    writer.Write(data->sampleBankId2);

    for(auto& instrument : data->instruments){
        auto crc = AudioContext::GetPathByAddr(instrument);
        writer.Write(crc);
    }

    for(auto& drum : data->drums){
        auto crc = AudioContext::GetPathByAddr(drum);
        writer.Write(crc);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SoundFontFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto entry = AudioContext::tables[AudioTableType::FONT_TABLE].at(offset);
    auto reader = AudioContext::MakeReader(AudioTableType::FONT_TABLE, entry.addr);
    auto font = std::make_shared<SoundFontData>();

    font->numInstruments = (entry.shortData2 >> 8) & 0xFFu;
    font->numDrums = entry.shortData2 & 0xFFu;
    font->sampleBankId1 = (entry.shortData1 >> 8) & 0xFFu;
    font->sampleBankId2 = entry.shortData1 & 0xFFu;

    uint32_t drumBaseAddr = reader.ReadUInt32();
    uint32_t instBaseAddr = 4;

    if(drumBaseAddr != 0){
        reader.Seek(entry.addr + drumBaseAddr, LUS::SeekOffsetType::Start);
        for(size_t i = 0; i < font->numDrums; i++){
            uint32_t addr = reader.ReadUInt32();

            if(addr == 0){
                font->drums.push_back(0);
                continue;
            }

            // SPDLOG_INFO("Found Drum[{}] 0x{:X}", i, addr);

            YAML::Node drum;
            drum["type"] = "NAUDIO:V1:DRUM";
            drum["parent"] = entry.addr;
            drum["offset"] = entry.addr + addr;
            drum["sampleBankId"] = (uint32_t) font->sampleBankId1;

            Companion::Instance->AddAsset(drum);
            font->drums.push_back(entry.addr + addr);
        }

//        YAML::Node table;
//        table["type"] = "ASSET_ARRAY";
//        table["assetType"] = "Drum";
//        table["factoryType"] = "NAUDIO:V1:DRUM";
//        table["offset"] = entry.addr + drumBaseAddr;
//        table["count"] = (uint32_t) font->numDrums;
//        Companion::Instance->AddAsset(table);
    }

    reader.Seek(entry.addr + instBaseAddr, LUS::SeekOffsetType::Start);
    for(size_t i = 0; i < font->numInstruments; i++){
        uint32_t addr = reader.ReadUInt32();

        if(addr == 0){
            font->instruments.push_back(0);
            continue;
        }

        // SPDLOG_INFO("Found Instrument[{}] 0x{:X}", i, addr);

        YAML::Node instrument;
        instrument["type"] = "NAUDIO:V1:INSTRUMENT";
        instrument["parent"] = entry.addr;
        instrument["offset"] = entry.addr + addr;
        instrument["sampleBankId"] = (uint32_t) font->sampleBankId1;
        font->instruments.push_back(entry.addr + addr);

        Companion::Instance->AddAsset(instrument);
    }

//    YAML::Node table;
//    table["type"] = "ASSET_ARRAY";
//    table["assetType"] = "Instrument";
//    table["factoryType"] = "NAUDIO:V1:INSTRUMENT";
//    table["offset"] = entry.addr + instBaseAddr;
//    table["count"] = (uint32_t) font->numInstruments;
//    Companion::Instance->AddAsset(table);

    return font;
}
