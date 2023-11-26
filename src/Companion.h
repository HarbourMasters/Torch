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

#define VERIFY_ENTRY(node, label, desc) \
    if (!node[label]) {                 \
        SPDLOG_ERROR(desc);             \
        return std::nullopt;            \
    }


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
};

class Companion {
public:
    static Companion* Instance;
    explicit Companion(std::filesystem::path rom, bool otr, bool debug) : gRomPath(std::move(rom)), gOTRMode(otr), gIsDebug(debug) {}
    void Init(ExportType type);
    void Process();
    bool IsOTRMode() { return this->gOTRMode; }
    bool IsDebug() { return this->gIsDebug; }
    N64::Cartridge* GetCartridge() { return this->gCartridge; }
    std::vector<uint8_t> GetRomData() { return this->gRomData; }
    std::string GetOutputPath() { return this->gOutputPath; }
    GBIVersion GetGBIVersion() { return this->gGBIVersion; }
    GBIMinorVersion GetGBIMinorVersion() { return this->gGBIMinorVersion; }
    std::unordered_map<std::string, std::vector<YAML::Node>> GetCourseMetadata() { return this->gCourseMetadata; }
    //bool CompareCourseId(const CourseMetadata& a, const CourseMetadata& b);

    //void SortCourseMetadata(void);
    std::optional<std::uint32_t> GetSegmentedAddr(uint8_t segment);
    std::optional<std::tuple<std::string, YAML::Node>> GetNodeByAddr(uint32_t addr);
    std::optional<std::shared_ptr<BaseFactory>> GetFactory(const std::string& type);

    static void Pack(const std::string& folder, const std::string& output);
    std::string NormalizeAsset(const std::string& name) const;
    void RegisterAsset(const std::string& name, YAML::Node& node);
private:
    bool gOTRMode = false;
    bool gIsDebug = false;
    GBIVersion gGBIVersion = GBIVersion::f3d;
    GBIMinorVersion gGBIMinorVersion = GBIMinorVersion::None;
    std::string gOutputPath;
    std::string gCurrentFile;
    std::vector<std::string> gTables;
    ExportType gExporterType;
    fs::path gCurrentDirectory;
    N64::Cartridge* gCartridge;
    std::vector<uint8_t> gRomData;
    std::filesystem::path gRomPath;
    std::vector<uint32_t> gSegments;
    std::unordered_map<std::string, std::vector<YAML::Node>> gCourseMetadata;

    std::variant<std::vector<std::string>, std::string> gWriteOrder;
    std::unordered_map<std::string, std::shared_ptr<BaseFactory>> gFactories;
    std::map<std::string, std::map<std::string, std::pair<YAML::Node, bool>>> gAssetDependencies;
    std::map<std::string, std::map<std::string, std::vector<std::pair<uint32_t, std::string>>>> gWriteMap;
    std::unordered_map<std::string, std::unordered_map<uint32_t, std::tuple<std::string, YAML::Node>>> gAddrMap;

    void ProcessTables(YAML::Node& rom);
    void RegisterFactory(const std::string& type, const std::shared_ptr<BaseFactory>& factory);
    void ExtractNode(YAML::Node& node, std::string& name, SWrapper* binary);
    void LoadYAMLRecursively(const std::string &dirPath, std::vector<YAML::Node> &result, bool skipRoot);
};
