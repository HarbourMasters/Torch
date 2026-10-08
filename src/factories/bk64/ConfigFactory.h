#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace BK64 {

// Classification of a >16MB BK ROM.
enum class RomhackKind {
    NotRomhack,  // vanilla-sized ROM
    BBRomhack,   // standard Banjo's Backpack build
    CustomBuild, // BK boot overlay present, but rebuilt/relocated internals
    UnknownRom,  // extended ROM with no recognizable BK boot overlay
};

// Binary aGameConfig format: 'BKCF' header + typed sections

constexpr uint32_t GAMECONFIG_MAGIC = 0x46434B42;
constexpr uint16_t GAMECONFIG_VERSION = 1;
// Its entry in modding.yml, and with .yaml its file in the modding folder.
constexpr const char* GAMECONFIG_MODDING_NAME = "assets/aGameConfig";

enum class ConfigSectionType : uint16_t {
    CODE_CONSTANTS = 1,
    SCENE_REMAP = 2,
    RETURN_TO_LAIR = 3,
    MUSIC = 4,
    SKYBOX = 5,
    SCENE_DEF = 6,
    NOTE_DOORS = 7,
    JIGGY_PUZZLES = 8,
    LEVEL_NAMES = 9,
    WARP_DESTINATIONS = 10,
    CUSTOM_CODE = 11,
    ROM_HASH = 12,
    CUSTOM_CODE_INFO = 13,
};

enum class CustomCodeKind : uint16_t {
    NONE = 0,
    BB_GLOBALIZATION = 1,
    BB_INJECTED = 2,
};

// CODE_CONSTANTS key IDs - they index into the sBBConfigs table.
enum class CodeConstantKey : uint16_t {
    NEW_GAME_MAP = 0,
    START_LEVEL_1,
    START_LEVEL_2,
    KNOW_ALL_MOVES,
    MUMBO_COST_TERMITE,
    MUMBO_COST_CROC,
    MUMBO_COST_WALRUS,
    MUMBO_COST_PUMPKIN,
    MUMBO_COST_BEE,
    EGGS_NORMAL_MAX,
    RED_FEATHERS_NORMAL_MAX,
    GOLD_FEATHERS_NORMAL_MAX,
    EGGS_CHEATO_MAX,
    RED_FEATHERS_CHEATO_MAX,
    GOLD_FEATHERS_CHEATO_MAX,
    NOTES_MAX,
    JIGGIES_PER_WORLD,
    HONEYCOMBS_PER_WORLD,
    EXTRA_HC_START,
    WARP_EXIT_BANJOS_HOUSE,
    WARP_ENTER_LAIR,
    SPECIAL_LEVEL,
    HIDE_JIGGIES_LEVEL,
    HIDE_COLLECTIBLES_LEVEL,
    COUNT
};

// Vanilla BK is exactly 16MB. Anything bigger has been through Banjo's Backpack.
inline bool IsRomhack(const std::vector<uint8_t>& rom) {
    return rom.size() > 0x1000000;
}

// The aGameConfig blob read back out. Each table keeps its entries in blob order, so writing one back
// gives the same bytes.
struct GameConfigData {
    struct Constant {
        uint16_t key;
        uint16_t value;
    };
    struct WorldMap {
        uint16_t map;
        uint16_t level;
    };
    struct LairReturn {
        uint8_t world;
        uint16_t map;
        uint16_t exit;
    };
    struct Music {
        uint16_t map;
        uint16_t track;
        uint16_t alternate;
    };
    struct SkyLayer {
        int16_t model;
        float scale;
        float spin; // degrees per second
    };
    struct Sky {
        uint16_t map;
        SkyLayer layers[3];
    };
    struct LevelModels {
        uint16_t map;
        int16_t opaque;
        int16_t translucent;
        int16_t cubeMin[3];
        int16_t cubeMax[3];
        float scale;
    };
    struct Title {
        uint8_t index;
        std::string text;
    };
    struct Warp {
        uint16_t warp;
        uint16_t dest; // map << 8 | entrance
    };
    struct NoteDoor {
        uint8_t door;
        uint16_t notes;
    };
    struct Puzzle {
        uint8_t picture;
        uint8_t cost;
        uint8_t size;
        uint16_t flag;
    };

    std::string romName;
    std::vector<Constant> constants;
    std::vector<WorldMap> worldByMap;
    std::vector<LairReturn> returnToLair;
    std::vector<Music> music;
    std::vector<Sky> skybox;
    std::vector<LevelModels> levelModels;
    std::vector<Title> titles;
    std::vector<Warp> warps;
    std::vector<NoteDoor> noteDoors;
    std::vector<Puzzle> puzzles;
    bool hasCustomCode = false;
    uint32_t customCodeRamBase = 0;
    uint8_t customCodeSha1[20] = {};
    CustomCodeKind customCodeKind = CustomCodeKind::NONE;
    bool hasRomHash = false;
    uint8_t romHash[20] = {};
};

bool ReadGameConfig(const std::vector<char>& blob, GameConfigData& out, std::string& error);
std::vector<char> WriteGameConfig(const GameConfigData& config);
std::string GameConfigToYaml(const GameConfigData& config);
bool GameConfigFromYaml(const std::string& text, GameConfigData& out, std::string& error);
const char* CodeConstantName(uint16_t key);

RomhackKind ClassifyRomhack(const std::vector<uint8_t>& rom);

// When a romhack, generate a config for it based on us v1.0.
bool TrySynthesizeRomConfig(YAML::Node& config, const std::string& cartHash, const std::filesystem::path& romPath,
                            const std::vector<uint8_t>& rom);

// Build the binary aGameConfig blob from a BB romhack ROM.
std::vector<char> BuildGameConfigBlob(const std::vector<uint8_t>& rom, const std::string& romName);

// True for injected custom-code blobs and for CustomBuild ROMs.
bool HasCustomCodeBlob(const std::vector<uint8_t>& rom);

// Pre-extraction probe for callers that decide the warning policy themselves
// (e.g. the extraction UI checks RomhackTable for ported hashes): reports
// the detected blob's kind and its SHA1 as 40 lowercase hex chars + NUL.
// Returns false (kind NONE, empty hash) when no blob is found.
bool GetCustomCodeBlobInfo(const std::vector<uint8_t>& rom, CustomCodeKind& outKind, char outSha1Hex[41]);

// Maintainer tool, creates a hashes.yml.
void EmitAssetHashes(const std::filesystem::path& romPath, const std::filesystem::path& outputPath);

// How many slots differ from the baseline hashes - a missing baseline entry counts as
// changed. The port-side extractor uses it to size its progress bar to the real romhack
// delta instead of every vanilla slot.
size_t CountModifiedSlots(const std::vector<uint8_t>& rom, const std::filesystem::path& hashesYamlPath);

// Baseline SHA-1 for one slot, or empty if there's no hashes.yaml or no entry for it.
// Loaded lazily from gAssetPath/hashes.yaml on the first call, then held for the run.
const std::string& GetBaselineAssetHash(uint32_t assetId);

// Vanilla warp destination (MAP << 8 | ENTRY) for a warp-table index, from the
// `warps:` section of hashes.yaml. Returns -1 if unknown. Loaded lazily like the
// baseline hashes.
int GetVanillaWarpDest(uint32_t warpIndex);

} // namespace BK64
