#pragma once

#include <string>
#include <optional>
#include <utility>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include "new_factories/BaseFactory.h"
#include "n64/Cartridge.h"

class Companion {
public:
    static Companion* Instance;
    explicit Companion(std::filesystem::path rom) : gRomPath(std::move(rom)) {}
    void Init(ExportType type);
    void Process();
    N64::Cartridge* GetCartridge() { return this->cartridge; }
    std::optional<std::tuple<std::string, YAML::Node>> GetNodeByAddr(uint32_t addr);

    static void Pack(const std::string& folder, const std::string& output);
private:
    ExportType gExporterType;
    std::filesystem::path gRomPath;
    std::vector<uint8_t> gRomData;
    std::unordered_map<std::string, std::shared_ptr<BaseFactory>> gFactories;
    N64::Cartridge* cartridge;
    std::string gCurrentFile;
    std::unordered_map<std::string, std::unordered_map<uint32_t, std::tuple<std::string, YAML::Node>>> gAddrMap;

    void RegisterFactory(const std::string& type, std::shared_ptr<BaseFactory> factory);
    std::optional<std::shared_ptr<BaseFactory>> GetFactory(const std::string& type);
};