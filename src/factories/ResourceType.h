#pragma once

namespace LUS {

enum class ResourceType {
    // Not set
    None = 0x00000000,

    // Common
    Archive = 0x4F415243,      // OARC (UNUSED)
    DisplayList = 0x4F444C54,  // ODLT
    Vertex = 0x4F565458,       // OVTX
    Matrix = 0x4F4D5458,       // OMTX
    Float = 0x4F464C54,        // OFLT
    Array = 0x4F415252,        // OARR
    Blob = 0x4F424C42,         // OBLB
    Texture = 0x4F544558,      // OTEX
    Bank = 0x42414E4B,         // BANK
    Sample = 0x41554643,       // AIFC
    Sequence = 0x53455143,     // SEQC
    Lights = 0x46669697,       // LGTS
    Vec3f = 0x56433346,        // VC3F
    Vec3s = 0x56433353,        // VC3S
    GenericArray = 0x47415252, // GARR

    // SM64
    Anim = 0x414E494D,         // ANIM
    SDialog = 0x53444C47,      // SDLG
    Dictionary = 0x44494354,   // DICT
    GeoLayout = 0x47454F20,    // GEO
    Collision = 0x434F4C20,    // COL
    MacroObject = 0x4D41434F,  // MACO
    Movtex = 0x4D4F5654,       // MOVT
    MovtexQuad = 0x4D4F5651,   // MOVQ

    // MK64
    CourseVertex = 0x43565458, // CVTX
    TrackSection = 0x5343544E, // SCTN
    Waypoints = 0x57505453,    // WPTS
    Metadata  = 0x4D444154,    // MDAT
    SpawnData = 0x53444154,    // SDAT
    DrivingBehaviour = 0x44424856, // DBHV

    // SF64
    AnimData = 0x414E494D,     // ANIM
    ColPoly = 0x43504C59,      // CPLY
    EnvSettings = 0x454E5653,  // ENVS
    Limb = 0x4C494D42,         // LIMB
    Message = 0x4D534720,      // MSG
    MessageTable = 0x4D534754, // MSGT
    Skeleton = 0x534B454C,     // SKEL
    Script = 0x53435250,       // SCRP
    ScriptCmd = 0x53434D44,    // SCMD
    Hitbox = 0x48544258,       // HTBX
    ObjectInit = 0x4F42494E,   // OBIN
};
} // namespace LUS
