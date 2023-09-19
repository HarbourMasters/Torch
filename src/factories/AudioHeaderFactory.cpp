#include "AudioHeaderFactory.h"

#include <vector>
#include "audio/AudioManager.h"

bool AudioHeaderFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
    AudioManager::Instance = new AudioManager();
    AudioManager::Instance->initialize(buffer, data);
    return false;
}