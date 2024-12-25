#include "InstrumentFactory.h"
#include "utils/Decompressor.h"
#include "Companion.h"
#include "EnvelopeFactory.h"
#include <tinyxml2.h>

ExportResult InstrumentHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Instrument " << symbol << ";\n";

    return std::nullopt;
}

ExportResult InstrumentCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    return std::nullopt;
}

ExportResult InstrumentBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<InstrumentData>(raw);

    WriteHeader(writer, Torch::ResourceType::Instrument, 0);
    writer.Write(data->isRelocated);
    writer.Write(data->normalRangeLo);
    writer.Write(data->normalRangeHi);
    writer.Write(data->adsrDecayIndex);
    writer.Write(AudioContext::GetPathByAddr(data->envelope));
    writer.Write(AudioContext::GetPathByAddr(data->lowPitchTunedSample.sample));
    writer.Write(data->lowPitchTunedSample.tuning);
    writer.Write(AudioContext::GetPathByAddr(data->normalPitchTunedSample.sample));
    writer.Write(data->normalPitchTunedSample.tuning);
    writer.Write(AudioContext::GetPathByAddr(data->highPitchTunedSample.sample));
    writer.Write(data->highPitchTunedSample.tuning);

    writer.Finish(write);
    return std::nullopt;
}

ExportResult InstrumentXMLExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto instrument = std::static_pointer_cast<InstrumentData>(raw);

    auto envelopeData = std::static_pointer_cast<EnvelopeData>(Companion::Instance->GetParseDataByAddr(instrument->envelope)->data.value());

    auto path = fs::path(*replacement);

    *replacement += ".meta";

    tinyxml2::XMLDocument inst;
    tinyxml2::XMLElement* root = inst.NewElement("Instrument");
    root->SetAttribute("NormalRangeLo", instrument->normalRangeLo);
    root->SetAttribute("NormalRangeHi", instrument->normalRangeLo);
    root->SetAttribute("ReleaseRate", instrument->adsrDecayIndex);

    tinyxml2::XMLElement* envelopes = inst.NewElement("Envelopes");
    for(auto& point : envelopeData->points){
        tinyxml2::XMLElement* pointEntry = envelopes->InsertNewChildElement("Point");
        pointEntry->SetAttribute("Delay", point.delay);
        pointEntry->SetAttribute("Arg", point.arg);
        envelopes->InsertEndChild(pointEntry);
    }

    auto lowSample = instrument->lowPitchTunedSample;
    auto normSample = instrument->normalPitchTunedSample;
    auto highSample = instrument->highPitchTunedSample;

    if(lowSample.sample != 0 && lowSample.tuning != 0.0f){
        tinyxml2::XMLElement* low = inst.NewElement("LowPitchSample");
        low->SetAttribute("Tuning", lowSample.tuning);
        low->SetAttribute("Path", std::get<std::string>(Companion::Instance->GetNodeByAddr(lowSample.sample).value()).c_str());

        root->InsertEndChild(low);
    }

    if(normSample.sample != 0 && normSample.tuning != 0.0f) {
        tinyxml2::XMLElement* normal = inst.NewElement("NormalPitchSample");
        normal->SetAttribute("Tuning", normSample.tuning);
        normal->SetAttribute("Path", std::get<std::string>(Companion::Instance->GetNodeByAddr(normSample.sample).value()).c_str());

        root->InsertEndChild(normal);
    }

    if(highSample.sample != 0 && highSample.tuning != 0.0f) {
        tinyxml2::XMLElement* high = inst.NewElement("HighPitchSample");
        high->SetAttribute("Tuning", highSample.tuning);
        high->SetAttribute("Path", std::get<std::string>(Companion::Instance->GetNodeByAddr(highSample.sample).value()).c_str());

        root->InsertEndChild(high);
    }

    inst.InsertEndChild(root);

    tinyxml2::XMLPrinter printer;
    inst.Accept(&printer);
    write.write(printer.CStr(), printer.CStrSize() - 1);

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> InstrumentFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto parent = GetSafeNode<uint32_t>(node, "parent");
    auto sampleBankId = GetSafeNode<uint32_t>(node, "sampleBankId");

    auto entry = AudioContext::tables[AudioTableType::FONT_TABLE].at(parent);

    auto reader = AudioContext::MakeReader(AudioTableType::FONT_TABLE, offset);

    auto instrument = std::make_shared<InstrumentData>();
    instrument->isRelocated = reader.ReadUByte();
    instrument->normalRangeLo = reader.ReadUByte();
    instrument->normalRangeHi = reader.ReadUByte();
    instrument->adsrDecayIndex = reader.ReadUByte();

    auto envAddr = entry.addr + reader.ReadUInt32();
    YAML::Node envelope;
    envelope["type"] = "NAUDIO:V1:ENVELOPE";
    envelope["offset"] = envAddr;
    instrument->envelope = envAddr;
    Companion::Instance->AddAsset(envelope);

    instrument->lowPitchTunedSample = AudioContext::LoadTunedSample(reader, parent, sampleBankId);
    instrument->normalPitchTunedSample = AudioContext::LoadTunedSample(reader, parent, sampleBankId);
    instrument->highPitchTunedSample = AudioContext::LoadTunedSample(reader, parent, sampleBankId);

    return instrument;
}
