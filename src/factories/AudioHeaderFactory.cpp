#include "AudioHeaderFactory.h"

#include <vector>
#include "Companion.h"
#include "audio/AudioManager.h"

void AudioAIFCExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) {

    auto samples = AudioManager::Instance->get_samples();

    int temp = 0;
    for(auto& sample : samples){
        LUS::BinaryWriter writer = LUS::BinaryWriter();
        std::string dpath = Companion::Instance->GetOutputPath() + "/" + (*replacement);
        if(!exists(fs::path(dpath).parent_path())){
            create_directories(fs::path(dpath).parent_path());
        }
        std::ofstream file(dpath + "_bank_" + std::to_string(++temp) + ".aifc", std::ios::binary);
        AudioManager::write_aifc(sample, writer);
        writer.Finish(file);
        file.close();
    }
}

std::optional<std::shared_ptr<IParsedData>> AudioHeaderFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    AudioManager::Instance->initialize(buffer, data);
    auto parsed = std::make_shared<IParsedData>();
    return parsed;
}