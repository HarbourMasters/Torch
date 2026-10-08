#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace BK64 {
// map_e and level_e in the decomp's enums.h, without the MAP_n_ and LEVEL_n_ prefixes.
inline constexpr const char* kMapNames[] = {
    "NIL",
    "SM_SPIRAL_MOUNTAIN",
    "MM_MUMBOS_MOUNTAIN",
    "STUB_TEST_TEMPLE",
    "STUB_TEST_MB",
    "TTC_BLUBBERS_SHIP",
    "TTC_NIPPERS_SHELL",
    "TTC_TREASURE_TROVE_COVE",
    "STUB_TEST_CS_HEIGHT_AND_DISTANCE",
    "STUB_TEST_CS_SLIGHT_SLOPES",
    "TTC_SANDCASTLE",
    "CC_CLANKERS_CAVERN",
    "MM_TICKERS_TOWER",
    "BGS_BUBBLEGLOOP_SWAMP",
    "MM_MUMBOS_SKULL",
    "STUB_TEST_PM",
    "BGS_MR_VILE",
    "BGS_TIPTUP",
    "GV_GOBIS_VALLEY",
    "GV_MEMORY_GAME",
    "GV_SANDYBUTTS_MAZE",
    "GV_WATER_PYRAMID",
    "GV_RUBEES_CHAMBER",
    "STUB_TEST_GS",
    "STUB_TEST_RL",
    "STUB_TEST_CS_STEEP_SLOPES",
    "GV_INSIDE_JINXY",
    "MMM_MAD_MONSTER_MANSION",
    "MMM_CHURCH",
    "MMM_CELLAR",
    "CS_START_NINTENDO",
    "CS_START_RAREWARE",
    "CS_END_NOT_100",
    "CC_WITCH_SWITCH_ROOM",
    "CC_INSIDE_CLANKER",
    "CC_GOLDFEATHER_ROOM",
    "MMM_TUMBLARS_SHED",
    "MMM_WELL",
    "MMM_NAPPERS_ROOM",
    "FP_FREEZEEZY_PEAK",
    "MMM_EGG_ROOM",
    "MMM_NOTE_ROOM",
    "MMM_FEATHER_ROOM",
    "MMM_SECRET_CHURCH_ROOM",
    "MMM_BATHROOM",
    "MMM_BEDROOM",
    "MMM_HONEYCOMB_ROOM",
    "MMM_WATERDRAIN_BARREL",
    "MMM_MUMBOS_SKULL",
    "RBB_RUSTY_BUCKET_BAY",
    "STUB_LL_LAVA_LEVEL",
    "STUB_DEMO_INTRO_ROOM",
    "RBB_ENGINE_ROOM",
    "RBB_WAREHOUSE",
    "RBB_BOATHOUSE",
    "RBB_CONTAINER_1",
    "RBB_CONTAINER_3",
    "RBB_CREW_CABIN",
    "RBB_BOSS_BOOM_BOX",
    "RBB_STORAGE_ROOM",
    "RBB_KITCHEN",
    "RBB_NAVIGATION_ROOM",
    "RBB_CONTAINER_2",
    "RBB_CAPTAINS_CABIN",
    "CCW_HUB",
    "FP_BOGGYS_IGLOO",
    "STUB_LL_TIPTUPS_CAVE",
    "CCW_SPRING",
    "CCW_SUMMER",
    "CCW_AUTUMN",
    "CCW_WINTER",
    "BGS_MUMBOS_SKULL",
    "FP_MUMBOS_SKULL",
    "STUB_ML_MUMBOS_SKULL",
    "CCW_SPRING_MUMBOS_SKULL",
    "CCW_SUMMER_MUMBOS_SKULL",
    "CCW_AUTUMN_MUMBOS_SKULL",
    "CCW_WINTER_MUMBOS_SKULL",
    "STUB_LL_MUMBOS_SKULL",
    "STUB_FL_MUMBOS_SKULL_NORMAL",
    "STUB_FL_MUMBOS_SKULL_RAIN_DANCE",
    "STUB_LL_SWIM",
    "STUB_LL_TEMPLE",
    "FP_CHRISTMAS_TREE",
    "STUB_ML_CRUSHER_SHED",
    "STUB_ML_GENERATOR_CAVES",
    "STUB_ML_MINE_LEVEL",
    "STUB_ML_TUBS",
    "STUB_ML_WATER_CAVES",
    "STUB_ML_WATERFALL",
    "CCW_SUMMER_ZUBBA_HIVE",
    "CCW_SPRING_ZUBBA_HIVE",
    "CCW_AUTUMN_ZUBBA_HIVE",
    "STUB_CCW_WINTER_ZUBBA_HIVE",
    "CCW_SPRING_NABNUTS_HOUSE",
    "CCW_SUMMER_NABNUTS_HOUSE",
    "CCW_AUTUMN_NABNUTS_HOUSE",
    "CCW_WINTER_NABNUTS_HOUSE",
    "CCW_WINTER_HONEYCOMB_ROOM",
    "CCW_AUTUMN_NABNUTS_WATER_SUPPLY",
    "CCW_WINTER_NABNUTS_WATER_SUPPLY",
    "CCW_SPRING_WHIPCRACK_ROOM",
    "CCW_SUMMER_WHIPCRACK_ROOM",
    "CCW_AUTUMN_WHIPCRACK_ROOM",
    "CCW_WINTER_WHIPCRACK_ROOM",
    "GL_MM_LOBBY",
    "GL_TTC_AND_CC_PUZZLE",
    "GL_180_NOTE_DOOR",
    "GL_RED_CAULDRON_ROOM",
    "GL_TTC_LOBBY",
    "GL_GV_LOBBY",
    "GL_FP_LOBBY",
    "GL_CC_LOBBY",
    "GL_STATUE_ROOM",
    "GL_BGS_LOBBY",
    "STUB_GL_ORIGINAL_FP_LOBBY",
    "GL_GV_PUZZLE",
    "GL_MMM_LOBBY",
    "GL_640_NOTE_DOOR",
    "GL_RBB_LOBBY",
    "GL_RBB_AND_MMM_PUZZLE",
    "GL_CCW_LOBBY",
    "GL_CRYPT",
    "CS_INTRO_GL_DINGPOT_1",
    "CS_INTRO_BANJOS_HOUSE_1",
    "CS_SPIRAL_MOUNTAIN_1",
    "CS_SPIRAL_MOUNTAIN_2",
    "FP_WOZZAS_CAVE",
    "GL_FF_ENTRANCE",
    "CS_INTRO_GL_DINGPOT_2",
    "CS_ENTERING_GL_MACHINE_ROOM",
    "CS_GAME_OVER_MACHINE_ROOM",
    "CS_UNUSED_MACHINE_ROOM",
    "CS_SPIRAL_MOUNTAIN_3",
    "CS_SPIRAL_MOUNTAIN_4",
    "CS_SPIRAL_MOUNTAIN_5",
    "CS_SPIRAL_MOUNTAIN_6",
    "CS_INTRO_BANJOS_HOUSE_2",
    "CS_INTRO_BANJOS_HOUSE_3",
    "RBB_ANCHOR_ROOM",
    "SM_BANJOS_HOUSE",
    "MMM_INSIDE_LOGGO",
    "GL_FURNACE_FUN",
    "TTC_SHARKFOOD_ISLAND",
    "GL_BATTLEMENTS",
    "FILE_SELECT",
    "GV_SNS_CHAMBER",
    "GL_DINGPOT",
    "CS_INTRO_SPIRAL_7",
    "CS_END_ALL_100",
    "CS_END_BEACH_1",
    "CS_END_BEACH_2",
    "CS_END_SPIRAL_MOUNTAIN_1",
    "CS_END_SPIRAL_MOUNTAIN_2",
    "UNUSED",
    "UNUSED",
    "UNUSED",
};
inline constexpr const char* kLevelNames[] = {
    "NONE", "MUMBOS_MOUNTAIN", "TREASURE_TROVE_COVE", "CLANKERS_CAVERN",  "BUBBLEGLOOP_SWAMP",   "FREEZEEZY_PEAK",
    "LAIR", "GOBIS_VALLEY",    "CLICK_CLOCK_WOOD",    "RUSTY_BUCKET_BAY", "MAD_MONSTER_MANSION", "SPIRAL_MOUNTAIN",
    "BOSS", "CUTSCENE",
};

inline const char* MapName(uint32_t map) {
    return map < sizeof(kMapNames) / sizeof(kMapNames[0]) ? kMapNames[map] : nullptr;
}

inline const char* LevelName(uint32_t level) {
    return level < sizeof(kLevelNames) / sizeof(kLevelNames[0]) ? kLevelNames[level] : nullptr;
}

// Takes SM_SPIRAL_MOUNTAIN or MAP_1_SM_SPIRAL_MOUNTAIN.
inline std::optional<uint16_t> MapFromName(const std::string& name) {
    for (uint16_t map = 1; map < sizeof(kMapNames) / sizeof(kMapNames[0]); map++) {
        const std::string shortName = kMapNames[map];
        if (name == shortName ||
            (name.rfind("MAP_", 0) == 0 && name.size() > shortName.size() &&
             name.compare(name.size() - shortName.size() - 1, std::string::npos, "_" + shortName) == 0)) {
            return map;
        }
    }
    return std::nullopt;
}

inline std::optional<uint16_t> LevelFromName(const std::string& name) {
    for (uint16_t level = 1; level < sizeof(kLevelNames) / sizeof(kLevelNames[0]); level++) {
        const std::string shortName = kLevelNames[level];
        if (name == shortName ||
            (name.rfind("LEVEL_", 0) == 0 && name.size() > shortName.size() &&
             name.compare(name.size() - shortName.size() - 1, std::string::npos, "_" + shortName) == 0)) {
            return level;
        }
    }
    return std::nullopt;
}
} // namespace BK64
