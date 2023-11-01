#pragma once

#include "RawFactory.h"

class DisplayListFactory : public RawFactory {
public:
    DisplayListFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};