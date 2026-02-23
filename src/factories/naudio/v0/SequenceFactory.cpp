#include "SequenceFactory.h"
#include "Companion.h"
#include <tinyxml2.h>

ExportResult SequenceBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    const auto sequence = std::static_pointer_cast<SequenceData>(raw);

    WriteHeader(writer, Torch::ResourceType::Sequence, 0);
    writer.Write(sequence->mId);
    writer.Write(static_cast<uint32_t>(sequence->mBanks.size()));
    for (auto& bank : sequence->mBanks) {
        writer.Write(bank);
    }
    writer.Write( sequence->mSize);
    writer.Write(reinterpret_cast<char*>(sequence->mBuffer.data()), sequence->mSize);
    writer.Finish(write);
    return std::nullopt;
}

ExportResult SequenceModdingExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    const auto sequence = std::static_pointer_cast<SequenceData>(raw);
    *replacement += ".m64";
    writer.Write(reinterpret_cast<char*>(sequence->mBuffer.data()), sequence->mSize);
    writer.Finish(write);
    return std::nullopt;
}

ExportResult SequenceXMLExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto sequence = std::static_pointer_cast<SequenceData>(raw);
    
    auto path = fs::path(*replacement);
    tinyxml2::XMLDocument seq;
    tinyxml2::XMLElement* root = seq.NewElement("Sequence");
    root->SetAttribute("ID", sequence->mId);
    root->SetAttribute("Path", (path.string() + "_data.m64").c_str());
    tinyxml2::XMLElement* banks = seq.NewElement("Banks");
    for (auto& bank : sequence->mBanks) {
        tinyxml2::XMLElement* entry = banks->InsertNewChildElement("Entry");
        entry->SetAttribute("Path", bank.c_str());
        banks->InsertEndChild(entry);
    }
    root->InsertEndChild(banks);
    seq.InsertEndChild(root);

    tinyxml2::XMLPrinter printer;
    seq.Accept(&printer);
    write.write(printer.CStr(), printer.CStrSize() - 1);

    auto data = (char*) sequence->mBuffer.data();
    std::vector<char> m64(data, data + sequence->mBuffer.size());
    Companion::Instance->RegisterCompanionFile(path.filename().string() + "_data.m64", m64);

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SequenceFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    auto id = data["id"].as<uint32_t>();
    auto size = data["size"].as<size_t>();
    const auto offset = data["offset"].as<size_t>();
    auto banks = data["banks"].as<std::vector<std::string>>();
    return std::make_shared<SequenceData>(id, size, buffer.data() + offset, banks);
}

std::optional<std::shared_ptr<IParsedData>> SequenceFactory::parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) {
    return std::make_shared<RawBuffer>(buffer.data(), buffer.size());
}