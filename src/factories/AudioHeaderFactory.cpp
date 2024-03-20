#include "AudioHeaderFactory.h"

#include <vector>
#include "Companion.h"
#include "audio/AudioManager.h"
#include "audio/AIFCDecode.h"
#include "spdlog/spdlog.h"

void AudioAIFCExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) {

    auto samples = AudioManager::Instance->get_samples();

    int temp = 0;
    for(auto& sample : samples){
        std::string dpath = Companion::Instance->GetOutputPath() + "/" + (*replacement);
        if(!exists(fs::path(dpath).parent_path())){
            create_directories(fs::path(dpath).parent_path());
        }
        std::ofstream file(dpath + "_bank_" + std::to_string(++temp) + ".aifc", std::ios::binary);

        LUS::BinaryWriter aifc = LUS::BinaryWriter();
        AudioManager::write_aifc(sample, aifc);
        // LUS::BinaryWriter aiff = LUS::BinaryWriter();
        // write_aiff(aifc.ToVector(), aiff);
        // aifc.Close();
        aifc.Finish(file);
        file.close();
        SPDLOG_INFO("Exported {}", dpath + "_bank_" + std::to_string(temp) + ".aif");
    }
}

std::optional<std::shared_ptr<IParsedData>> AudioHeaderFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    AudioManager::Instance->initialize(buffer, data);
    auto parsed = std::make_shared<IParsedData>();
    return parsed;
}