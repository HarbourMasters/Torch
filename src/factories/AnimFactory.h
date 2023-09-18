#pragma once

#include "RawFactory.h"

class AnimFactory : public RawFactory {
public:
    AnimFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};