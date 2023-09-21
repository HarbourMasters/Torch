#pragma once

#include "../RawFactory.h"

class MIO0Factory : public RawFactory {
public:
    MIO0Factory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};