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
    Array = 0x4F415252,        // OARR
    Blob = 0x4F424C42,         // OBLB
    Texture = 0x4F544558,      // OTEX
    Bank = 0x42414E4B,         // BANK
    Sample = 0x41554643,       // AIFC
    Sequence = 0x53455143,     // SEQC
    Lights = 0x46669697,       // LGTS

    // SM64
    Anim = 0x414E494D,         // ANIM
    SDialog = 0x53444C47,      // SDLG
    Dictionary = 0x44494354,   // DICT
    GeoLayout = 0x47454F20,    // GEO

    // MK64
    CourseVertex = 0x43565458, // CVTX
    TrackSection = 0x5343544E, // SCTN
    Waypoints = 0x57505453,    // WPTS
    Metadata  = 0x4D444154,    // MDAT
    SpawnData = 0x53444154,    // SDAT

    // SF64
    AnimData = 0x414E494D,     // ANIM
    Message = 0x4D53474C,      // MSG
    MessageTable = 0x4D534754, // MSGT
};
} // namespace LUS
