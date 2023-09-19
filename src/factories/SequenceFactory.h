#pragma once

#include "RawFactory.h"

class SequenceFactory : public RawFactory {
public:
    SequenceFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};