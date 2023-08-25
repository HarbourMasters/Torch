#pragma once

#include "RawFactory.h"

class BlobFactory : public RawFactory {
public:
    BlobFactory(LUS::ResourceType type, bool isMIOChunk = false, bool bigEndian = false) : type(type), isMIOChunk(isMIOChunk), bigEndian(bigEndian) {}
    bool process(LUS::BinaryWriter* write, nlohmann::json& data, std::vector<uint8_t>& buffer) override;
private:
    bool isMIOChunk = false;
    bool bigEndian = false;
    LUS::ResourceType type = LUS::ResourceType::Blob;
};