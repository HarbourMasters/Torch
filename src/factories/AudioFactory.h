#pragma once

#include "RawFactory.h"

class AudioFactory : public RawFactory {
public:
    AudioFactory() = default;
    bool process(LUS::BinaryWriter* write, nlohmann::json& data, std::vector<uint8_t>& buffer) override;
};