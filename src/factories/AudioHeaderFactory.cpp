#include "AudioHeaderFactory.h"

#include <vector>
#include "audio/AudioManager.h"

std::optional<std::shared_ptr<IParsedData>> AudioHeaderFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    AudioManager::Instance = new AudioManager();
    AudioManager::Instance->initialize(buffer, data);
    return std::nullopt;
}