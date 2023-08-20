#pragma once

#include "RawFactory.h"

class BlobFactory : public RawFactory {
public:
    BlobFactory(LUS::ResourceType type, bool isMIOChunk = false) : type(type), isMIOChunk(isMIOChunk) {}
    bool process(LUS::BinaryWriter* write, nlohmann::json& data, std::vector<uint8_t>& buffer) override;
private:
    bool isMIOChunk = false;
    LUS::ResourceType type = LUS::ResourceType::Blob;
};