#pragma once

#include "../RawFactory.h"

class SDictionaryFactory : public RawFactory {
public:
    SDictionaryFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};