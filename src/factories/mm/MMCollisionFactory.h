#pragma once

#include "factories/oot/OoTCollisionFactory.h"

namespace MM {

class MMCollisionFactory : public OoT::OoTCollisionFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& node) override;
};

} // namespace MM
