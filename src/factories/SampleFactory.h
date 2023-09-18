#pragma once

#include "RawFactory.h"

class SampleFactory : public RawFactory {
public:
    SampleFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};