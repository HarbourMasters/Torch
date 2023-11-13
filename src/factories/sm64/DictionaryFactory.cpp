#include "DictionaryFactory.h"
#include "utils/MIODecoder.h"

void SM64::DictionaryBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<DictionaryData>(raw);

    WriteHeader(writer, LUS::ResourceType::Dictionary, 0);

    writer.Write((uint32_t) data->mDictionary.size());
    for(auto& [key, value] : data->mDictionary){
        writer.Write(key);
        writer.Write((uint32_t) value.size());
        writer.Write((char*) value.data(), value.size());
    }
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SM64::DictionaryFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    std::unordered_map<std::string, std::vector<uint8_t>> dictionary;

    for (auto it = data["keys"].begin(); it != data["keys"].end(); ++it) {
        auto key = it->first.as<std::string>();
        auto offset = it->second.as<size_t>();

        std::vector<uint8_t> text;
        auto bytes = (uint8_t*) buffer.data();

        while(bytes[offset] != 0xFF){
            auto c = bytes[offset++];
            text.push_back(c);
        }

        text.push_back(0xFF);
        dictionary[key] = text;
    }

    return std::make_shared<DictionaryData>(dictionary);
}