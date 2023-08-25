#pragma once

#include "RawFactory.h"

class AnimFactory : public RawFactory {
public:
    AnimFactory() = default;
    bool process(LUS::BinaryWriter* write, nlohmann::json& data, std::vector<uint8_t>& buffer) override;
};