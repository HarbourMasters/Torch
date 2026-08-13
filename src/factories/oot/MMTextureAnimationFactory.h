#pragma once

#include "factories/BaseFactory.h"

namespace OoT {

// Majora's Mask animated materials. OoT has no equivalent, so this is MM-only
// rather than a branch inside a shared factory. Mirrors OTRExporter's
// TextureAnimationExporter.cpp and ZAPD's ZTextureAnimation.
class MMTextureAnimationBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MMTextureAnimationFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, MMTextureAnimationBinaryExporter)
        };
    }
};

// Serialize an animated material list at a segmented address. Shared with the
// scene command writer, which discovers these rather than having them declared.
std::vector<char> SerializeTextureAnimation(std::vector<uint8_t>& buffer, uint32_t segAddr,
                                            const std::string& resPath);

} // namespace OoT
