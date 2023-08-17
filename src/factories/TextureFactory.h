#pragma once

#include "RawFactory.h"

class TextureFactory : public RawFactory {
public:
    TextureFactory() = default;
    void process(LUS::BinaryWriter* write, nlohmann::json& data, std::vector<uint8_t>& buffer) override;
};