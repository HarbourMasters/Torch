#pragma once

#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#define CREATOR_NINTENDO 4

namespace FZX {
enum class Venue {
    /*  0 */ VENUE_MUTE_CITY,
    /*  1 */ VENUE_PORT_TOWN,
    /*  2 */ VENUE_BIG_BLUE,
    /*  3 */ VENUE_SAND_OCEAN,
    /*  4 */ VENUE_DEVILS_FOREST,
    /*  5 */ VENUE_WHITE_LAND,
    /*  6 */ VENUE_SECTOR,
    /*  7 */ VENUE_RED_CANYON,
    /*  8 */ VENUE_FIRE_FIELD,
    /*  9 */ VENUE_SILENCE,
    /* 10 */ VENUE_ENDING,
};

enum class Skybox {
    /* 0 */ SKYBOX_PURPLE,
    /* 1 */ SKYBOX_TURQUOISE,
    /* 2 */ SKYBOX_DESERT,
    /* 3 */ SKYBOX_BLUE,
    /* 4 */ SKYBOX_NIGHT,
    /* 5 */ SKYBOX_ORANGE,
    /* 6 */ SKYBOX_SUNSET,
    /* 7 */ SKYBOX_SKY_BLUE,
};

enum class PitZone {
    /* -1 */ PIT_NONE = -1,
    /*  0 */ PIT_BOTH,
    /*  1 */ PIT_LEFT,
    /*  2 */ PIT_RIGHT,
};

enum class DashZone {
    /* -1 */ DASH_NONE = -1,
    /*  0 */ DASH_MIDDLE,
    /*  1 */ DASH_LEFT,
    /*  2 */ DASH_RIGHT,
};

enum class Dirt {
    /* -1 */ DIRT_NONE = -1,
    /*  0 */ DIRT_BOTH,
    /*  1 */ DIRT_LEFT,
    /*  2 */ DIRT_RIGHT,
    /*  3 */ DIRT_MIDDLE,
};

enum class Ice {
    /* -1 */ ICE_NONE = -1,
    /*  0 */ ICE_BOTH,
    /*  1 */ ICE_LEFT,
    /*  2 */ ICE_RIGHT,
    /*  3 */ ICE_MIDDLE,
};

enum class Jump {
    /* -1 */ JUMP_NONE = -1,
    /*  0 */ JUMP_ALL,
    /*  1 */ JUMP_LEFT,
    /*  2 */ JUMP_RIGHT,
};

enum class Landmine {
    /* -1 */ LANDMINE_NONE = -1,
    /*  0 */ LANDMINE_MIDDLE,
    /*  1 */ LANDMINE_LEFT,
    /*  2 */ LANDMINE_RIGHT,
};

enum class Gate {
    /* -1 */ GATE_NONE = -1,
    /*  0 */ GATE_SQUARE,
    /*  1 */ GATE_START,
    /*  2 */ GATE_HEXAGONAL,
};

enum class Building {
    /* -1 */ BUILDING_NONE = -1,
    /*  0 */ BUILDING_TALL_BOTH,
    /*  1 */ BUILDING_TALL_LEFT,
    /*  2 */ BUILDING_TALL_RIGHT,
    /*  3 */ BUILDING_SHORT_BOTH,
    /*  4 */ BUILDING_SHORT_LEFT,
    /*  5 */ BUILDING_SHORT_RIGHT,
    /*  6 */ BUILDING_SPIRE_BOTH,
    /*  7 */ BUILDING_SPIRE_LEFT,
    /*  8 */ BUILDING_SPIRE_RIGHT,
    /*  9 */ BUILDING_MOUNTAIN_BOTH,
    /* 10 */ BUILDING_MOUNTAIN_LEFT,
    /* 11 */ BUILDING_MOUNTAIN_RIGHT,
    /* 12 */ BUILDING_TALL_GOLD_BOTH,
    /* 13 */ BUILDING_TALL_GOLD_LEFT,
    /* 14 */ BUILDING_TALL_GOLD_RIGHT,
};

enum class Sign {
    /* -1 */ SIGN_NONE = -1,
    /*  0 */ SIGN_TV,
    /*  1 */ SIGN_1,
    /*  2 */ SIGN_2,
    /*  3 */ SIGN_NINTEX,
    /*  4 */ SIGN_OVERHEAD,
};

inline std::ostream& operator<<(std::ostream& out, const Venue& venue) {
    std::string output;
    switch (venue) {
        case Venue::VENUE_MUTE_CITY:
            output = "VENUE_MUTE_CITY";
            break;
        case Venue::VENUE_PORT_TOWN:
            output = "VENUE_PORT_TOWN";
            break;
        case Venue::VENUE_BIG_BLUE:
            output = "VENUE_BIG_BLUE";
            break;
        case Venue::VENUE_SAND_OCEAN:
            output = "VENUE_SAND_OCEAN";
            break;
        case Venue::VENUE_DEVILS_FOREST:
            output = "VENUE_DEVILS_FOREST";
            break;
        case Venue::VENUE_WHITE_LAND:
            output = "VENUE_WHITE_LAND";
            break;
        case Venue::VENUE_SECTOR:
            output = "VENUE_SECTOR";
            break;
        case Venue::VENUE_RED_CANYON:
            output = "VENUE_RED_CANYON";
            break;
        case Venue::VENUE_FIRE_FIELD:
            output = "VENUE_FIRE_FIELD";
            break;
        case Venue::VENUE_SILENCE:
            output = "VENUE_SILENCE";
            break;
        case Venue::VENUE_ENDING:
            output = "VENUE_ENDING";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const Skybox& skybox) {
    std::string output;
    switch (skybox) {
        case Skybox::SKYBOX_PURPLE:
            output = "SKYBOX_PURPLE";
            break;
        case Skybox::SKYBOX_TURQUOISE:
            output = "SKYBOX_TURQUOISE";
            break;
        case Skybox::SKYBOX_DESERT:
            output = "SKYBOX_DESERT";
            break;
        case Skybox::SKYBOX_BLUE:
            output = "SKYBOX_BLUE";
            break;
        case Skybox::SKYBOX_NIGHT:
            output = "SKYBOX_NIGHT";
            break;
        case Skybox::SKYBOX_ORANGE:
            output = "SKYBOX_ORANGE";
            break;
        case Skybox::SKYBOX_SUNSET:
            output = "SKYBOX_SUNSET";
            break;
        case Skybox::SKYBOX_SKY_BLUE:
            output = "SKYBOX_SKY_BLUE";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const PitZone& pitZone) {
    std::string output;
    switch (pitZone) {
        case PitZone::PIT_NONE:
            output = "PIT_NONE";
            break;
        case PitZone::PIT_BOTH:
            output = "PIT_BOTH";
            break;
        case PitZone::PIT_LEFT:
            output = "PIT_LEFT";
            break;
        case PitZone::PIT_RIGHT:
            output = "PIT_RIGHT";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const DashZone& dashZone) {
    std::string output;
    switch (dashZone) {
        case DashZone::DASH_NONE:
            output = "DASH_NONE";
            break;
        case DashZone::DASH_MIDDLE:
            output = "DASH_MIDDLE";
            break;
        case DashZone::DASH_LEFT:
            output = "DASH_LEFT";
            break;
        case DashZone::DASH_RIGHT:
            output = "DASH_RIGHT";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const Dirt& dirt) {
    std::string output;
    switch (dirt) {
        case Dirt::DIRT_NONE:
            output = "DIRT_NONE";
            break;
        case Dirt::DIRT_BOTH:
            output = "DIRT_BOTH";
            break;
        case Dirt::DIRT_LEFT:
            output = "DIRT_LEFT";
            break;
        case Dirt::DIRT_RIGHT:
            output = "DIRT_RIGHT";
            break;
        case Dirt::DIRT_MIDDLE:
            output = "DIRT_MIDDLE";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const Ice& ice) {
    std::string output;
    switch (ice) {
        case Ice::ICE_NONE:
            output = "ICE_NONE";
            break;
        case Ice::ICE_BOTH:
            output = "ICE_BOTH";
            break;
        case Ice::ICE_LEFT:
            output = "ICE_LEFT";
            break;
        case Ice::ICE_RIGHT:
            output = "ICE_RIGHT";
            break;
        case Ice::ICE_MIDDLE:
            output = "ICE_MIDDLE";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const Jump& jump) {
    std::string output;
    switch (jump) {
        case Jump::JUMP_NONE:
            output = "JUMP_NONE";
            break;
        case Jump::JUMP_ALL:
            output = "JUMP_ALL";
            break;
        case Jump::JUMP_LEFT:
            output = "JUMP_LEFT";
            break;
        case Jump::JUMP_RIGHT:
            output = "JUMP_RIGHT";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const Landmine& landmine) {
    std::string output;
    switch (landmine) {
        case Landmine::LANDMINE_NONE:
            output = "LANDMINE_NONE";
            break;
        case Landmine::LANDMINE_MIDDLE:
            output = "LANDMINE_MIDDLE";
            break;
        case Landmine::LANDMINE_LEFT:
            output = "LANDMINE_LEFT";
            break;
        case Landmine::LANDMINE_RIGHT:
            output = "LANDMINE_RIGHT";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const Gate& gate) {
    std::string output;
    switch (gate) {
        case Gate::GATE_NONE:
            output = "GATE_NONE";
            break;
        case Gate::GATE_SQUARE:
            output = "GATE_SQUARE";
            break;
        case Gate::GATE_START:
            output = "GATE_START";
            break;
        case Gate::GATE_HEXAGONAL:
            output = "GATE_HEXAGONAL";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const Building& building) {
    std::string output;
    switch (building) {
        case Building::BUILDING_NONE:
            output = "BUILDING_NONE";
            break;
        case Building::BUILDING_TALL_BOTH:
            output = "BUILDING_TALL_BOTH";
            break;
        case Building::BUILDING_TALL_LEFT:
            output = "BUILDING_TALL_LEFT";
            break;
        case Building::BUILDING_TALL_RIGHT:
            output = "BUILDING_TALL_RIGHT";
            break;
        case Building::BUILDING_SHORT_BOTH:
            output = "BUILDING_SHORT_BOTH";
            break;
        case Building::BUILDING_SHORT_LEFT:
            output = "BUILDING_SHORT_LEFT";
            break;
        case Building::BUILDING_SHORT_RIGHT:
            output = "BUILDING_SHORT_RIGHT";
            break;
        case Building::BUILDING_SPIRE_BOTH:
            output = "BUILDING_SPIRE_BOTH";
            break;
        case Building::BUILDING_SPIRE_LEFT:
            output = "BUILDING_SPIRE_LEFT";
            break;
        case Building::BUILDING_SPIRE_RIGHT:
            output = "BUILDING_SPIRE_RIGHT";
            break;
        case Building::BUILDING_MOUNTAIN_BOTH:
            output = "BUILDING_MOUNTAIN_BOTH";
            break;
        case Building::BUILDING_MOUNTAIN_LEFT:
            output = "BUILDING_MOUNTAIN_LEFT";
            break;
        case Building::BUILDING_MOUNTAIN_RIGHT:
            output = "BUILDING_MOUNTAIN_RIGHT";
            break;
        case Building::BUILDING_TALL_GOLD_BOTH:
            output = "BUILDING_TALL_GOLD_BOTH";
            break;
        case Building::BUILDING_TALL_GOLD_LEFT:
            output = "BUILDING_TALL_GOLD_LEFT";
            break;
        case Building::BUILDING_TALL_GOLD_RIGHT:
            output = "BUILDING_TALL_GOLD_RIGHT";
            break;
    }
    return out << output;
}

inline std::ostream& operator<<(std::ostream& out, const Sign& sign) {
    std::string output;
    switch (sign) {
        case Sign::SIGN_NONE:
            output = "SIGN_NONE";
            break;
        case Sign::SIGN_TV:
            output = "SIGN_TV";
            break;
        case Sign::SIGN_1:
            output = "SIGN_1";
            break;
        case Sign::SIGN_2:
            output = "SIGN_2";
            break;
        case Sign::SIGN_NINTEX:
            output = "SIGN_NINTEX";
            break;
        case Sign::SIGN_OVERHEAD:
            output = "SIGN_OVERHEAD";
            break;
    }
    return out << output;
}
}
