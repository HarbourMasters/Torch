#include "SequenceFactory.h"

#include "audio/AudioManager.h"
#include <vector>

bool SequenceFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
    WRITE_HEADER(LUS::ResourceType::Sequence, 0);

    auto id = data["id"].as<uint32_t>();
    auto size = data["size"].as<size_t>();
    auto offset = data["offset"].as<size_t>();

    auto banks = data["banks"].as<std::vector<std::string>>();

    WRITE_U32(id);
    WRITE_U32(banks.size());
    for(auto& bank : banks){
        writer->Write(bank);
    }

    WRITE_U32(size);
    WRITE_ARRAY(buffer.data() + offset, size);

    return true;
}