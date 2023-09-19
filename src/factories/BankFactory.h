#pragma once

#include "RawFactory.h"

class BankFactory : public RawFactory {
public:
    BankFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};