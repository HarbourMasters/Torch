#pragma once

#include "factories/oot/OoTAudioFactory.h"

namespace MM {

class MMAudioFactory : public OoT::OoTAudioFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& node) override;
};

} // namespace MM
