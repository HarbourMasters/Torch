#pragma once

#include "RawFactory.h"

class AudioHeaderFactory : public RawFactory {
public:
    AudioHeaderFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};