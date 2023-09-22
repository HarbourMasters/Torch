#pragma once

#include "factories/RawFactory.h"

class SAnimFactory : public RawFactory {
public:
    SAnimFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};