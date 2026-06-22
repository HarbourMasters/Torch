#pragma once

#include <string>
#include <optional>
#include <filesystem>
#include <vector>
#include <atomic>
#include <array>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <functional>
#include "factories/BaseFactory.h"
#include "n64/Cartridge.h"
#include "utils/Decompressor.h"
#include "factories/TextureFactory.h"

#define CONTAINS(_map, value) ((_map).find(value) != (_map).end())

class BinaryWrapper;
namespace fs = std::filesystem;

enum class ParseMode {
    Default,
    Directory
};

enum class GBIVersion {
    f3db,
    f3d,
    f3dex,
    f3dexb,
    f3dex2,
};

enum class GBIMinorVersion {
    None,
    Mk64,
    SM64,
    PM64,
    BK64
};

enum class TableMode {
    Reference,
    Append
};

enum class ArchiveType {
    None,
    OTR,
    O2R,
};

struct SegmentConfig {
    std::unordered_map<uint32_t, uint32_t> global;
    std::unordered_map<uint32_t, uint32_t> local;
    std::unordered_map<uint32_t, uint32_t> temporal;
    std::unordered_map<std::string, std::unordered_map<uint32_t, std::pair<std::uint32_t, std::uint32_t>>> compressed;
};

struct Table {
    std::string name;
    uint32_t start;
    uint32_t end;
    TableMode mode;
    int32_t index_size;
};

struct VRAMEntry {
    uint32_t addr;
    uint32_t offset;
};

struct WriteEntry {
    std::string name;
    uint32_t addr;
    uint32_t alignment;
    std::string buffer;
    std::optional<std::string> comment;
    std::optional<uint32_t> endptr;
};

struct GBIConfig {
    GBIVersion version = GBIVersion::f3d;
    GBIMinorVersion subversion = GBIMinorVersion::None;
    bool useFloats = false;
};

struct TorchConfig {
    GBIConfig gbi;
    SegmentConfig segment;
    std::string outputPath;
    std::string moddingPath;
    ExportType exporterType;
    ParseMode parseMode;
    ArchiveType otrMode;
    bool debug;
    bool modding;
    bool textureDefines;
    bool includeAutogen;
    bool dialogPack = false;
};

struct ParseResultData {
    std::string name;
    std::string type;
    YAML::Node node;
    std::optional<std::shared_ptr<IParsedData>> data;

    uint32_t GetOffset() {
        return GetSafeNode<uint32_t>(node, "offset");
    }

    std::optional<std::string> GetSymbol() {
        return GetSafeNode<std::string>(node, "symbol");
    }
};

class Companion {
public:
    static Companion* Instance;

    explicit Companion(std::filesystem::path rom, const ArchiveType otr, const bool debug, const bool modding = false,
                       const std::string& srcDir = "", const std::string& destPath = "") : gCartridge(nullptr),
                       gSourceDirectory(srcDir), gDestinationDirectory(destPath) {
        this->gRomPath = rom;
        this->gConfig.otrMode = otr;
        this->gConfig.debug = debug;
        this->gConfig.modding = modding;
        this->gConfig.textureDefines = false;
        this->gConfig.includeAutogen = false;
    }

    explicit Companion(std::vector<uint8_t> rom, const ArchiveType otr, const bool debug, const bool modding = false,
                       const std::string& srcDir = "", const std::string& destPath = "") : gCartridge(nullptr),
                       gSourceDirectory(srcDir), gDestinationDirectory(destPath) {
        this->gRomData = rom;
        this->gConfig.otrMode = otr;
        this->gConfig.debug = debug;
        this->gConfig.modding = modding;
        this->gConfig.textureDefines = false;
        this->gConfig.includeAutogen = false;
    }

    void SetRomPath(std::filesystem::path path) { this->gRomPath = std::move(path); }

    explicit Companion(std::filesystem::path rom, const ArchiveType otr, const bool debug, const std::string& srcDir = "", const std::string& destPath = "") :
                       Companion(rom, otr, debug, false, srcDir, destPath) {}
    
    explicit Companion(std::vector<uint8_t> rom, const ArchiveType otr, const bool debug, const std::string& srcDir = "", const std::string& destPath = "") :
                       Companion(rom, otr, debug, false, srcDir, destPath) {}

    void Init(ExportType type);
    void Init(ExportType type, std::atomic<size_t>& assetCount, bool shouldProcess);

    bool NodeHasChanges(const std::string& string);

    void Process(std::atomic<size_t>& assetCount);

    bool IsOTRMode() const { return (this->gConfig.otrMode != ArchiveType::None); }
    bool IsDebug() const { return this->gConfig.debug; }
    bool AddTextureDefines() const { return this->gConfig.textureDefines; }

    N64::Cartridge* GetCartridge() const { return this->gCartridge.get(); }
    std::vector<uint8_t>& GetRomData() { return this->gRomData; }
    std::string GetOutputPath() { return this->gConfig.outputPath; }
    const std::string& GetAssetPath() const { return this->gAssetPath; }
    std::string GetDestRelativeOutputPath() { return RelativePathToDestDir(GetOutputPath()); }

    GBIVersion GetGBIVersion() const { return this->gConfig.gbi.version; }
    GBIMinorVersion GetGBIMinorVersion() const { return  this->gConfig.gbi.subversion; }
    std::unordered_map<std::string, std::vector<YAML::Node>> GetCourseMetadata() { return this->gCourseMetadata; }
    std::optional<std::string> GetEnumFromValue(const std::string& key, int id);
    bool IsUsingIndividualIncludes() const { return this->gIndividualIncludes; }

    std::optional<ParseResultData> GetParseDataByAddr(uint32_t addr);
    std::optional<ParseResultData> GetParseDataBySymbol(const std::string& symbol);

    std::optional<std::uint32_t> GetFileOffsetFromSegmentedAddr(uint8_t segment) const;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> GetFileOffsetFromCompressedSegmentedAddr(uint8_t segment) const;
    std::optional<std::shared_ptr<BaseFactory>> GetFactory(const std::string& type);
    uint32_t PatchVirtualAddr(uint32_t addr);
    std::optional<std::tuple<std::string, YAML::Node>> GetNodeByAddr(uint32_t addr);
    std::optional<std::string> GetStringByAddr(uint32_t addr);
    std::optional<std::tuple<std::string, YAML::Node>> GetSafeNodeByAddr(const uint32_t addr, std::string type);
    std::optional<std::string> GetSafeStringByAddr(const uint32_t addr, std::string type);
    std::optional<std::vector<std::tuple<std::string, YAML::Node>>> GetNodesByType(const std::string& type, bool includeAutogen = false);
    // Same as GetNodesByType but returns a pointer into the internal cache to
    // avoid copying. The pointer is valid until the cache is invalidated; do not
    // hold across AddAsset/RegisterAsset calls. Returns nullptr if the current
    // file has no nodes registered.
    const std::vector<std::tuple<std::string, YAML::Node>>* GetNodesByTypeRef(const std::string& type, bool includeAutogen = false);
    std::string GetSymbolFromAddr(uint32_t addr, bool validZero = false);

    std::optional<std::uint32_t> GetFileOffset(void) const { return this->gCurrentFileOffset; };
    std::optional<std::uint32_t> GetCurrSegmentNumber(void) const { return this->gCurrentSegmentNumber; };
    CompressionType GetCurrCompressionType(void) const { return this->gCurrentCompressionType; };
    std::optional<std::uint32_t> GetCurrentCompressedSize(void) const { return this->gCurrentCompressedSize; };
    std::optional<VRAMEntry> GetCurrentVRAM(void) const { return this->gCurrentVram; };
    std::optional<Table> SearchTable(uint32_t addr);

    static std::vector<char> ParseVersionString(const std::string& version);
    static std::string CalculateHash(const std::vector<uint8_t>& data);
    static void Pack(const std::string& folder, const std::string& output, const ArchiveType otrMode, const std::string& version = "");
    std::string NormalizeAsset(const std::string& name) const;
    std::string RelativePath(const std::string& path) const;
    std::string RelativePathToSrcDir(const std::string& path) const;
    std::string RelativePathToDestDir(const std::string& path) const;
    void RegisterCompanionFile(const std::string path, std::vector<char> data);
    void RegisterArchiveFile(const std::string& name, std::vector<char> data);
    void SetAdditionalFiles(const std::vector<std::string>& files) { this->gAdditionalFiles = files; }
    void SetVersion(const std::string& version) { this->gVersion = version; }

    std::string GetCurrentAssetName() const { return this->gCurrentAssetName; }
    void SetAssetTotal(std::atomic<size_t>* total) { this->gAssetTotal = total; }
    void SetPhaseCallback(std::function<void(int)> cb) { this->gPhaseCallback = cb; }

    void SetProcess(bool shouldProcess);
    TorchConfig& GetConfig() { return this->gConfig; }
    BinaryWrapper* GetCurrentWrapper() { return this->gCurrentWrapper; }
    const std::unordered_map<std::string, std::string>& GetModdedAssetPaths() const { return this->gModdedAssetPaths; }

    std::optional<std::tuple<std::string, YAML::Node>> RegisterAsset(const std::string& name, YAML::Node& node);
    std::optional<YAML::Node> AddSubFileAsset(YAML::Node asset, std::string newFileName, CompressionType newCompressionType, uint32_t compressedSize = 0);
    std::optional<YAML::Node> AddAsset(YAML::Node asset);
    void SetCompressedSegment(uint32_t segmentId, uint32_t compressedFileOffset, uint32_t offset);
    bool GetCompressedSegmentOffset(uint32_t* addr);
private:
    TorchConfig gConfig;
    YAML::Node gModdingConfig;
    fs::path gSourceDirectory;
    fs::path gDestinationDirectory;
    fs::path gCurrentDirectory;
    std::string gCurrentHash;
    std::string gAssetPath;
    std::string gVersion;
    std::vector<uint8_t> gRomData;
    std::optional<std::filesystem::path> gRomPath;
    bool gNodeForceProcessing = false;
    bool gIndividualIncludes = false;
    YAML::Node gHashNode;
    std::shared_ptr<N64::Cartridge> gCartridge;
    std::unordered_map<std::string, std::vector<YAML::Node>> gCourseMetadata;
    std::unordered_map<std::string, std::unordered_map<int32_t, std::string>> gEnums;
    BinaryWrapper* gCurrentWrapper;

    // Temporal Variables
    std::string gCurrentAssetName;
    std::atomic<size_t>* gAssetCounter = nullptr;
    std::atomic<size_t>* gAssetTotal = nullptr;
    std::function<void(int)> gPhaseCallback;
    std::string gCurrentFile;
    std::vector<std::string> gSubFileList;
    std::string gCurrentVirtualPath;
    std::string gFileHeader;
    bool gEnablePadGen = false;
    bool mShouldProcess = true;
    uint32_t gCurrentPad = 0;
    uint32_t gCurrentFileOffset;
    uint32_t gCurrentSegmentNumber;
    std::optional<VRAMEntry> gCurrentVram;
    CompressionType gCurrentCompressionType = CompressionType::None;
    std::optional<std::uint32_t> gCurrentCompressedSize;
    std::vector<Table> gTables;
    std::vector<std::string> gCurrentExternalFiles;
    std::unordered_map<int, std::string> gManualSegments;
    std::unordered_set<std::string> gProcessedFiles;

    std::unordered_map<std::string, std::vector<char>> gCompanionFiles;
    std::vector<std::pair<std::string, std::vector<char>>> gArchiveFiles;
    std::unordered_map<std::string, std::vector<ParseResultData>> gParseResults;
    std::vector<std::string> gAdditionalFiles;

    std::unordered_map<std::string, std::string> gModdedAssetPaths;
    std::variant<std::vector<std::string>, std::string> gWriteOrder;
    std::unordered_map<std::string, std::shared_ptr<BaseFactory>> gFactories;
    std::unordered_map<std::string, std::map<std::string, std::vector<WriteEntry>>> gWriteMap;
    std::unordered_map<std::string, std::tuple<uint32_t, uint32_t>> gVirtualAddrMap;
    std::unordered_map<std::string, std::unordered_map<uint32_t, std::tuple<std::string, YAML::Node>>> gAddrMap;
    // Cached results of GetNodesByType keyed by [file][type][includeAutogen?].
    // Built lazily; invalidated on RegisterAsset for the affected (file, type).
    // Index 0 = !includeAutogen, index 1 = includeAutogen.
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::array<std::vector<std::tuple<std::string, YAML::Node>>, 2>>>
        gNodesByTypeCache;
    // Validity bitmap; bit 0 = !includeAutogen, bit 1 = includeAutogen.
    std::unordered_map<std::string, std::unordered_map<std::string, uint8_t>> gNodesByTypeCacheValid;

    void ProcessParseFile(YAML::Node root, std::atomic<size_t>& assetCount);
    void ProcessExportFile();
    void ProcessFile(YAML::Node root);
    void ProcessFile(YAML::Node root, std::atomic<size_t>& assetCount);
    void ParseEnums(std::string& file);
    void ParseHash();
    void ParseModdingConfig();
    void ParseCurrentFileConfig(YAML::Node node, std::atomic<size_t>& assetCount);
    void RegisterFactory(const std::string& type, const std::shared_ptr<BaseFactory>& factory);
    void ExtractNode(YAML::Node& node, std::string& name, BinaryWrapper* binary);
    void ProcessTables(YAML::Node& rom);
    void LoadYAMLRecursively(const std::string &dirPath, std::vector<YAML::Node> &result, bool skipRoot);
    std::optional<ParseResultData> ParseNode(YAML::Node& node, std::string& name);
};
