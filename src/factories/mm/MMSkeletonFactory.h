#pragma once

#include "factories/oot/OoTSkeletonFactory.h"

namespace MM {

class MMSkeletonFactory : public OoT::OoTSkeletonFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& node) override;
};

} // namespace MM
