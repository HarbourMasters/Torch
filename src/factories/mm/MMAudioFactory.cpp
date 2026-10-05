#include "MMAudioFactory.h"
#include "MMAudioFontWriter.h"

namespace MM {

std::optional<std::shared_ptr<IParsedData>> MMAudioFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    YAML::Node mmNode = YAML::Clone(node);
    mmNode["offset"] = 0x80000000;
    auto result = OoT::OoTAudioFactory::parse(buffer, mmNode);
    MMAudioFontWriter().NormalizeNullInstruments();
    return result;
}

} // namespace MM
