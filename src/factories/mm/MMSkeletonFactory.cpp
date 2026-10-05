#include "MMSkeletonFactory.h"

#include "factories/oot/OoTSkeletonTypes.h"

namespace MM {

std::optional<std::shared_ptr<IParsedData>> MMSkeletonFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto parsed = OoT::OoTSkeletonFactory::parse(buffer, node);
    if (!parsed.has_value() || !node["limb_table_count"]) {
        return parsed;
    }

    const auto skeleton = std::static_pointer_cast<OoT::OoTSkeletonData>(parsed.value());

    const uint8_t limbCount = node["limb_table_count"].as<uint8_t>();
    if (skeleton->limbPaths.size() > limbCount) {
        skeleton->limbPaths.resize(limbCount);
    }
    return parsed;
}

} // namespace MM
