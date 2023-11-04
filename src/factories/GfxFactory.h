#pragma once

#include "RawFactory.h"

class GfxFactory : public RawFactory {

public:
    GfxFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};