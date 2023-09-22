#pragma once

#include "../RawFactory.h"

class STextFactory : public RawFactory {
public:
    STextFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};