#pragma once

#include <string>
#include <optional>
#include <filesystem>
#include <vector>
#include <unordered_map>
#include "factories/BaseFactory.h"
#include "n64/Cartridge.h"

class SWrapper;
namespace fs = std::filesystem;

class Companion {
public:
    static Companion* Instance;
    explicit Companion(std::filesystem::path rom, bool otr) : gRomPath(std::move(rom)), gOTRMode(otr) {}
    void Init(ExportType type);
    void Process();
    bool IsOTRMode() { return this->gOTRMode; }
    N64::Cartridge* GetCartridge() { return this->gCartridge; }
    std::vector<uint8_t> GetRomData() { return this->gRomData; }
    std::optional<std::uint32_t> GetSegmentedAddr(uint8_t segment);
    std::optional<std::tuple<std::string, YAML::Node>> GetNodeByAddr(uint32_t addr);
    std::optional<std::shared_ptr<BaseFactory>> GetFactory(const std::string& type);

    static void Pack(const std::string& folder, const std::string& output);
    std::string NormalizeAsset(const std::string& name) const;
    void RegisterAsset(const std::string& name, YAML::Node& node);
private:
    ExportType gExporterType;
    bool gOTRMode = false;
    std::filesystem::path gRomPath;
    std::vector<uint8_t> gRomData;
    std::unordered_map<std::string, std::shared_ptr<BaseFactory>> gFactories;
    N64::Cartridge* gCartridge;
    std::string gCurrentFile;
    fs::path gCurrentDirectory;
    std::vector<uint32_t> gSegments;
    std::map<std::string, std::map<std::string, std::pair<YAML::Node, bool>>> gAssetDependencies;
    std::unordered_map<std::string, std::unordered_map<uint32_t, std::tuple<std::string, YAML::Node>>> gAddrMap;

    void RegisterFactory(const std::string& type, const std::shared_ptr<BaseFactory>& factory);
    void ExtractNode(std::ostringstream& stream, YAML::Node& node, std::string& name, SWrapper* binary);
};
