#include "SampleFactory.h"

#include <vector>
#include "audio/AudioManager.h"

bool SampleFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
    WRITE_HEADER(LUS::ResourceType::Sample, 0);

    auto id = data["id"].as<int32_t>();
    AudioBankSample entry = AudioManager::Instance->get_aifc(id);

    // Write AdpcmLoop
    WRITE_U32(entry.loop.start);
    WRITE_U32(entry.loop.end);
    WRITE_I32(entry.loop.count);
    WRITE_U32(entry.loop.pad);

    if(entry.loop.state.has_value()){
        std::vector<int16_t> state = entry.loop.state.value();
        WRITE_U32(state.size());
        for(auto &value : state){
            writer->Write(value);
        }
    } else {
        WRITE_U32(0);
    }

    // Write AdpcmBook
    WRITE_I32(entry.book.order);
    WRITE_I32(entry.book.npredictors);

    WRITE_U32(entry.book.table.size());
    for(auto &value : entry.book.table){
        writer->Write(value);
    }

    // Write sample
    WRITE_I32(entry.data.size());
    for(auto &value : entry.data){
        WRITE_U8(value);
    }

    writer->Write(entry.name);

    return true;
}