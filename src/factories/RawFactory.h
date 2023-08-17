#pragma once

#include "ResourceType.h"
#include <string>
#include <vector>
#include <binarytools/BinaryWriter.h>
#include <nlohmann/json.hpp>

#define WRITE_HEADER(type, version) this->WriteHeader(writer, type, version)
#define WRITE_U32(value) writer->Write((uint32_t) value)
#define WRITE_U64(value) writer->Write((uint64_t) value)
#define WRITE_DATA(vec) writer->Write((char*) vec.data(), data.size())
#define WRITE_ARRAY(data, size) writer->Write((char*) data, size)

class RawFactory {
public:
    RawFactory() = default;
    virtual void process(LUS::BinaryWriter* write, nlohmann::json& data, std::vector<uint8_t>& buffer) = 0;
    void WriteHeader(LUS::BinaryWriter* write, LUS::ResourceType resType, int32_t version);
};