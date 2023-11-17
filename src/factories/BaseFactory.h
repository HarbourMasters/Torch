#pragma once

#include "ResourceType.h"
#include "n64/Cartridge.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <yaml-cpp/yaml.h>
#include <strhash64/StrHash64.h>
#include <binarytools/BinaryWriter.h>
#include <binarytools/BinaryReader.h>

#define REGISTER(type, c) { ExportType::type, std::make_shared<c>() },

#define SEGMENT_OFFSET(a) ((uint32_t)(a)&0x00FFFFFF)
#define SEGMENT_NUMBER(x) ((x >> 24) & 0xFF)

#define tab "\t"
#define fourSpaceTab "    "

enum class ExportType {
    Header,
    Code,
    Binary,
};

class IParsedData {};

template<typename T>
class GenericData : public IParsedData {
public:
    T mData;
};

class BaseExporter {
public:
    virtual void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) = 0;
    static void WriteHeader(LUS::BinaryWriter& write, LUS::ResourceType resType, int32_t version);
};

class BaseFactory {
public:
    virtual std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) = 0;
    std::optional<std::shared_ptr<BaseExporter>> GetExporter(ExportType type) {
        auto exporters = this->GetExporters();
        if (exporters.find(type) != exporters.end()) {
            return exporters[type];
        }
        return std::nullopt;
    }
private:
    virtual std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() = 0;
};