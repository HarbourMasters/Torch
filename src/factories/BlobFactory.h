#pragma once

#include "RawFactory.h"

class BlobFactory : public RawFactory {
public:
    BlobFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};