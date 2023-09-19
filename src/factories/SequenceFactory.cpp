#include "SequenceFactory.h"

#include "audio/AudioManager.h"
#include <vector>

bool SequenceFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
    WRITE_HEADER(LUS::ResourceType::Sequence, 0);

    auto size = data["size"].as<size_t>();
    auto offset = data["offset"].as<size_t>();

    auto bank = data["banks"].as<std::vector<uint8_t>>();

    WRITE_U32(bank.size());
    WRITE_ARRAY(bank.data(), bank.size());

    WRITE_U32(size);
    WRITE_ARRAY(buffer.data() + offset, size);

    return true;
}