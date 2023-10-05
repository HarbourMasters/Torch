#include "SDictionaryFactory.h"

#include "utils/MIODecoder.h"

namespace fs = std::filesystem;

bool SDictionaryFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {

    WRITE_HEADER(LUS::ResourceType::Dictionary, 0);

    auto entries = data["keys"].size();
    WRITE_U32(entries);

    // Iterate on yaml keys
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

        writer->Write(key);
        WRITE_U32(text.size());
        WRITE_ARRAY(text.data(), text.size());
    }

    return true;
}
