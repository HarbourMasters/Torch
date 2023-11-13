#include "SampleFactory.h"
#include "utils/MIODecoder.h"

void SampleBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto sample = std::static_pointer_cast<SampleData>(raw)->mSample;

    WriteHeader(writer, LUS::ResourceType::Sample, 0);
    writer.Write((uint32_t) sample.loop.start);
    writer.Write((uint32_t) sample.loop.end);
    writer.Write((uint32_t) sample.loop.count);
    writer.Write((uint32_t) sample.loop.pad);

    if(sample.loop.state.has_value()){
        auto state = sample.loop.state.value();
        writer.Write((uint32_t) state.size());
        writer.Write((char*) state.data(), state.size() * sizeof(int16_t));
    } else {
        writer.Write((uint32_t) 0);
    }

    writer.Write((uint32_t) sample.book.order);
    writer.Write((uint32_t) sample.book.npredictors);

    writer.Write((uint32_t) sample.book.table.size());
    writer.Write((char*) sample.book.table.data(), sample.book.table.size() * sizeof(int16_t));
    writer.Write(sample.name);

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SampleFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    auto id = data["id"].as<int32_t>();
    AudioBankSample entry = AudioManager::Instance->get_aifc(id);
    return std::make_shared<SampleData>(entry);
}