#pragma once

#include "ResourceType.h"
#include "n64/Cartridge.h"
#include <string>
#include <vector>
#include <filesystem>
#include <binarytools/BinaryWriter.h>
#include <yaml-cpp/yaml.h>
#include <strhash64/StrHash64.h>


#define SEGMENT_OFFSET(a) ((uint32_t)(a)&0x00FFFFFF)
#define SEGMENT_NUMBER(x) ((x >> 24) & 0xFF)

#define WRITE_HEADER(type, version) this->WriteHeader(writer, type, version)
#define WRITE_BHEADER(type, version) this->WriteHeader(writer, type, version, true)
#define WRITE_U8(value) writer->Write((uint8_t) value)
#define WRITE_U16(value) writer->Write((uint16_t) value)
#define WRITE_U32(value) writer->Write((uint32_t) value)
#define WRITE_U64(value) writer->Write((uint64_t) value)
#define WRITE_I8(value) writer->Write((int8_t) value)
#define WRITE_I16(value) writer->Write((int16_t) value)
#define WRITE_I32(value) writer->Write((int32_t) value)
#define WRITE_DATA(vec) writer->Write((char*) vec.data(), data.size())
#define WRITE_ARRAY(data, size) writer->Write((char*) data, size)

class RawFactory {
public:
    RawFactory() = default;
    virtual bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) = 0;
    void WriteHeader(LUS::BinaryWriter* write, LUS::ResourceType resType, int32_t version);
    inline void SetCartridge(N64::Cartridge* cart) {
        this->cartdrige = cart;
    }
    inline N64::Cartridge* GetCartridge() {
        return this->cartdrige;
    }
private:
    N64::Cartridge* cartdrige;
};