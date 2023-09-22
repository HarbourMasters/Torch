#pragma once

#include "../RawFactory.h"

class SDialogFactory : public RawFactory {
public:
    SDialogFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};