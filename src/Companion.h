#pragma once

#include <string>
#include <optional>
#include <filesystem>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <variant>
#include "factories/BaseFactory.h"
#include "n64/Cartridge.h"
#include "utils/Decompressor.h"

class SWrapper;
namespace fs = std::filesystem;

enum class GBIVersion {
    f3d,
    f3dex,
    f3db,
    f3dex2,
    f3dexb,
};

enum class GBIMinorVersion {
    None,
    Mk64,
    SM64
};

struct SegmentConfig {
    std::unordered_map<uint32_t, uint32_t> global;
    std::unordered_map<uint32_t, uint32_t> local;
    std::unordered_map<uint32_t, uint32_t> temporal;
};

struct GBIConfig {
    GBIVersion version = GBIVersion::f3d;
    GBIMinorVersion subversion = GBIMinorVersion::None;
};

struct TorchConfig {
    GBIConfig gbi;
    SegmentConfig segment;
    std::string outputPath;
    ExportType exporterType;
    bool otrMode;
    bool debug;
};

class Companion {
public:
    static Companion* Instance;

    explicit Companion(std::filesystem::path rom, const bool otr, const bool debug) : gRomPath(std::move(rom)), gCartridge(nullptr) {
        this->gConfig.otrMode = otr;
        this->gConfig.debug = debug;
    }

    void Init(ExportType type);
    void Process();

    bool IsOTRMode() const { return this->gConfig.otrMode; }
    bool IsDebug() const { return this->gConfig.debug; }

    N64::Cartridge* GetCartridge() const { return this->gCartridge.get(); }
    std::vector<uint8_t> GetRomData() { return this->gRomData; }
    std::string GetOutputPath() { return this->gConfig.outputPath; }

    GBIVersion GetGBIVersion() const { return this->gConfig.gbi.version; }
    GBIMinorVersion GetGBIMinorVersion() const { return  this->gConfig.gbi.subversion; }

    std::optional<std::uint32_t> GetFileOffsetFromSegmentedAddr(uint8_t segment) const;
    std::optional<std::tuple<std::string, YAML::Node>> GetNodeByAddr(uint32_t addr);
    std::optional<std::shared_ptr<BaseFactory>> GetFactory(const std::string& type);

    std::optional<std::uint32_t> GetFileOffset(void) const { return this->gCurrentFileOffset; };
    std::optional<std::uint32_t> GetCurrSegmentNumber(void) const { return this->gCurrentSegmentNumber; };
    CompressionType GetCurrCompressionType(void) const { return this->gCurrentCompressionType; };
    CompressionType GetCompressionType(std::vector<uint8_t>& buffer, const uint32_t offset);

    static void Pack(const std::string& folder, const std::string& output);
    std::string NormalizeAsset(const std::string& name) const;

    TorchConfig& GetConfig() { return this->gConfig; }

    std::optional<std::tuple<std::string, YAML::Node>> RegisterAsset(const std::string& name, YAML::Node& node);
private:
    TorchConfig gConfig;
    fs::path gCurrentDirectory;
    std::vector<uint8_t> gRomData;
    std::filesystem::path gRomPath;
    std::shared_ptr<N64::Cartridge> gCartridge;

    // Temporal Variables
    std::string gCurrentFile;
    std::string gFileHeader;
    std::variant<std::vector<std::string>, std::string> gWriteOrder;
    std::unordered_map<std::string, std::shared_ptr<BaseFactory>> gFactories;
    std::map<std::string, std::map<std::string, std::pair<YAML::Node, bool>>> gAssetDependencies;
    std::map<std::string, std::map<std::string, std::vector<std::pair<uint32_t, std::string>>>> gWriteMap;
    std::unordered_map<std::string, std::unordered_map<uint32_t, std::tuple<std::string, YAML::Node>>> gAddrMap;
    uint32_t gCurrentFileOffset;
    uint32_t gCurrentSegmentNumber;
    CompressionType gCurrentCompressionType;

    void ParseCurrentFileConfig(YAML::Node node);
    void RegisterFactory(const std::string& type, const std::shared_ptr<BaseFactory>& factory);
    void ExtractNode(YAML::Node& node, std::string& name, SWrapper* binary);
};
