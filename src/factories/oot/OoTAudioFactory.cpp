#ifdef OOT_SUPPORT

#include "OoTAudioFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

std::optional<std::shared_ptr<IParsedData>> OoTAudioFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto data = std::make_shared<OoTAudioData>();

    // Build the main audio entry: just a 64-byte OAUD header with version 2
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudio, 2);
    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    data->mMainEntry = std::vector<char>(str.begin(), str.end());

    SPDLOG_INFO("OoTAudioFactory: main audio entry ({} bytes)", data->mMainEntry.size());

    return data;
}

ExportResult OoTAudioBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto audio = std::static_pointer_cast<OoTAudioData>(raw);
    write.write(audio->mMainEntry.data(), audio->mMainEntry.size());
    return std::nullopt;
}

} // namespace OoT

#endif
