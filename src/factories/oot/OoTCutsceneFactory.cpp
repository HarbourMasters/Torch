#include "OoTCutsceneFactory.h"
#include "OoTSceneUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

#include <set>

namespace OoT {

// Camera commands use variable-length entries terminated by a 0xFF marker byte.
bool CutsceneSerializer::IsCameraCmd(uint32_t id) {
    return id == 1 || id == 2 || id == 5 || id == 6;
}

// These commands use 0x0C-byte entries instead of the standard 0x30.
bool CutsceneSerializer::IsSmallEntryCmd(uint32_t id) {
    return id == 0x09 || id == 0x13 || id == 0x8C;
}

// Single-entry commands (transition: 0x2D, destination: 0x3E8).
bool CutsceneSerializer::IsSingleEntryCmd(uint32_t id) {
    return id == 0x2D || id == 0x3E8;
}

// Commands that are in the valid range (0x0E–0x90) but not implemented.
const std::set<uint32_t> CutsceneSerializer::sUnimplementedCmds = {
    0x0B, 0x0C, 0x0D, 0x14, 0x15, 0x16, 0x1A, 0x1B, 0x1C, 0x20, 0x21, 0x38, 0x3B, 0x3D,
    0x47, 0x49, 0x5B, 0x5C, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x6D, 0x70, 0x71, 0x7A
};

bool CutsceneSerializer::IsHandledCmd(uint32_t cid) {
    // Basic commands (camera, misc, rumble)
    if (cid >= 0x01 && cid <= 0x0A) return true;

    // Transition and destination
    if (IsSingleEntryCmd(cid)) return true;

    // Text, time, and non-actor-cue commands
    if (cid == 0x13 || cid == 0x56 || cid == 0x57 || cid == 0x7C || cid == 0x8C) return true;

    // Actor cue commands (0x0E–0x90 range, excluding known unimplemented ones)
    if (cid >= 0x0E && cid <= 0x90 && !sUnimplementedCmds.count(cid)) return true;

    return false;
}

// Cutscene serialization — shared with OoTSceneFactory's SetCutscenes handler.
// Walk through cutscene commands to determine total byte size.
// Returns 0 if the data is corrupt or too small.
uint32_t CutsceneSerializer::CalculateSize(std::vector<uint8_t>& buffer, uint32_t segAddr) {
    auto reader = ReadSubArray(buffer, segAddr, 0x10000);
    uint32_t endOffset = reader.GetLength();

    // Need at least numCommands + endFrame
    if (endOffset < 8) return 0;

    uint32_t numCommands = reader.ReadUInt32();
    reader.ReadUInt32(); // endFrame

    for (uint32_t cmd = 0; cmd < numCommands; cmd++) {
        // Each command is at least 8 bytes (id + entryCount)
        if (reader.GetBaseAddress() + 8 > endOffset) return 0;

        uint32_t cmdId = reader.ReadUInt32();
        if (cmdId == 0xFFFFFFFF) break; // end marker

        uint32_t entryCount = reader.ReadUInt32();

        if (IsCameraCmd(cmdId)) {
            // Camera commands have an extra uint32 before the entry list
            if (reader.GetBaseAddress() + 4 > endOffset) return 0;

            reader.ReadUInt32();
            for (uint32_t ci = 0; ci < 1000; ci++) {
                // Each camera entry is 0x10 bytes
                if (reader.GetBaseAddress() + 0x10 > endOffset) return 0;

                uint8_t marker = reader.ReadUByte();
                reader.Seek(reader.GetBaseAddress() + 0x0F, LUS::SeekOffsetType::Start);
                if (marker == 0xFF) break; // end of camera entries
            }
            continue;
        }

        if (IsSingleEntryCmd(cmdId)) {
            // Single entry is 0x08 bytes
            if (reader.GetBaseAddress() + 0x08 > endOffset) return 0;

            reader.Seek(reader.GetBaseAddress() + 0x08, LUS::SeekOffsetType::Start);
            continue;
        }

        // Default: fixed-size entries (0x0C for rumble/text/time, 0x30 for everything else)
        uint32_t entrySize = IsSmallEntryCmd(cmdId) ? 0x0C : 0x30;
        uint32_t dataSize = entryCount * entrySize;

        // Ensure all entries fit within the cutscene data
        if (reader.GetBaseAddress() + dataSize > endOffset) return 0;

        reader.Seek(reader.GetBaseAddress() + dataSize, LUS::SeekOffsetType::Start);
    }

    // Read the end marker (0xFFFFFFFF + 0x00000000) to get final size
    if (reader.GetBaseAddress() + 8 > endOffset) return 0;
    reader.ReadUInt32();
    reader.ReadUInt32();

    return reader.GetBaseAddress();
}

void CutsceneSerializer::WriteCameraCmd(LUS::BinaryReader& reader, LUS::BinaryWriter& w) {
    uint16_t startFrame = reader.ReadUInt16();
    uint16_t endFrame = reader.ReadUInt16();
    w.Write(CS_CMD_HH(startFrame, endFrame));

    uint16_t unk1 = reader.ReadUInt16();
    uint16_t unk2 = reader.ReadUInt16();
    w.Write(CS_CMD_HH(unk1, unk2));

    while (true) {
        int8_t marker = reader.ReadInt8();
        int8_t interpType = reader.ReadInt8();
        int16_t numPointsForFrame = reader.ReadInt16();
        float viewAngle = reader.ReadFloat();
        int16_t posX = reader.ReadInt16();
        int16_t posY = reader.ReadInt16();
        int16_t posZ = reader.ReadInt16();
        int16_t unused = reader.ReadInt16();

        w.Write(CS_CMD_BBH(marker, interpType, numPointsForFrame));
        w.Write(viewAngle);
        w.Write(CS_CMD_HH(posX, posY));
        w.Write(CS_CMD_HH(posZ, unused));

        if ((uint8_t)marker == 0xFF) break;
    }
}

void CutsceneSerializer::WriteSingleEntryCmd(LUS::BinaryReader& reader, LUS::BinaryWriter& w) {
    reader.ReadUInt32(); // skip original count
    w.Write(static_cast<uint32_t>(1));

    uint16_t startFrame = reader.ReadUInt16();
    uint16_t endFrame = reader.ReadUInt16();
    uint16_t type = reader.ReadUInt16();
    reader.ReadUInt16(); // padding

    w.Write(CS_CMD_HH(startFrame, endFrame));
    w.Write(CS_CMD_HH(type, type));
}

// Helper: check if a command uses actor cue format (0x30-byte entries with rotation fields)
static bool IsActorCueCmd(uint32_t cid) {
    return cid != 0x03 && cid != 0x04 && cid != 0x56 && cid != 0x57 && cid != 0x7C;
}

void CutsceneSerializer::WriteEntryCountCmd(uint32_t cid, LUS::BinaryReader& reader, LUS::BinaryWriter& w) {
    uint32_t entryCount = reader.ReadUInt32();
    w.Write(entryCount);

    for (uint32_t i = 0; i < entryCount; i++) {
        // All entry types share a common header
        uint16_t base = reader.ReadUInt16();
        uint16_t startFrame = reader.ReadUInt16();
        uint16_t endFrame = reader.ReadUInt16();

        w.Write(CS_CMD_HH(base, startFrame));

        if (cid == 0x09) {
            // Rumble (0x0C bytes)
            uint8_t sourceStrength = reader.ReadUByte();
            uint8_t duration = reader.ReadUByte();
            uint8_t decreaseRate = reader.ReadUByte();
            uint8_t unk = reader.ReadUByte();
            uint16_t unkA = reader.ReadUInt16();

            w.Write(CS_CMD_HBB(endFrame, sourceStrength, duration));
            w.Write(CS_CMD_BBH(decreaseRate, unk, unkA));
            continue;
        }

        if (cid == 0x13) {
            // Text (0x0C bytes)
            uint16_t type = reader.ReadUInt16();
            uint16_t textId1 = reader.ReadUInt16();
            uint16_t textId2 = reader.ReadUInt16();

            w.Write(CS_CMD_HH(endFrame, type));
            w.Write(CS_CMD_HH(textId1, textId2));
            continue;
        }

        if (cid == 0x8C) {
            // Time (0x0C bytes)
            uint8_t hour = reader.ReadUByte();
            uint8_t minute = reader.ReadUByte();
            reader.ReadUInt32(); // padding

            w.Write(CS_CMD_HBB(endFrame, hour, minute));
            w.Write(static_cast<uint32_t>(0));
            continue;
        }

        // Actor cue (0x30 bytes)
        if (IsActorCueCmd(cid)) {
            w.Write(CS_CMD_HH(endFrame, reader.ReadUInt16()));
            uint16_t rotY = reader.ReadUInt16();
            uint16_t rotZ = reader.ReadUInt16();
            w.Write(CS_CMD_HH(rotY, rotZ));
            for (int j = 0; j < 9; j++) w.Write(reader.ReadUInt32());
            continue;
        }

        // Non-actor-cue (0x30 bytes, commands 0x03, 0x04, 0x56, 0x57, 0x7C)
        w.Write(CS_CMD_HH(endFrame, reader.ReadUInt16()));
        for (int j = 0; j < 10; j++) w.Write(reader.ReadUInt32());
    }
}

namespace {

// Majora's Mask cutscene command classification, from ZCutscene::GetCommandMM.
// Everything funnels into a handful of shapes; the id ranges are the awkward part.
constexpr uint32_t MM_CS_TEXT = 10;
constexpr uint32_t MM_CS_CAMERA_SPLINE = 90;
constexpr uint32_t MM_CS_MISC = 150;
constexpr uint32_t MM_CS_TRANSITION_GENERAL = 250;
constexpr uint32_t MM_CS_FADE_OUT_SEQ = 254;
constexpr uint32_t MM_CS_TIME = 255;
constexpr uint32_t MM_CS_PLAYER_CUE = 300;
constexpr uint32_t MM_CS_RUMBLE = 400;

bool MMIsActorCue(uint32_t id) {
    return (id >= 100 && id <= 149) || id == 201 || (id >= 450 && id <= 599) || id == MM_CS_PLAYER_CUE;
}

// Bytes per entry in the rom.
uint32_t MMEntryRawSize(uint32_t id) {
    if (MMIsActorCue(id)) {
        return 0x30;
    }
    switch (id) {
        case MM_CS_TEXT:
        case MM_CS_TRANSITION_GENERAL:
        case MM_CS_FADE_OUT_SEQ:
        case MM_CS_TIME:
        case MM_CS_RUMBLE:
            return 0x0C;
        default:
            return 0x08;
    }
}

} // namespace

// Walk the rom commands to find the cutscene's total byte length. Mirrors
// ZCutscene::ParseRawData: read the id, hand the rest to the command, then
// advance by its size less the four bytes already consumed.
uint32_t CutsceneSerializer::CalculateSizeMM(std::vector<uint8_t>& buffer, uint32_t segAddr) {
    auto probe = ReadSubArray(buffer, segAddr, 8);
    if (probe.GetLength() < 8) {
        return 0;
    }
    uint32_t numCommands = probe.ReadUInt32();
    if (numCommands > 0x1000) {
        return 0;
    }

    uint32_t pos = 8;
    for (uint32_t i = 0; i < numCommands; i++) {
        auto head = ReadSubArray(buffer, segAddr + pos, 8);
        if (head.GetLength() < 8) {
            return 0;
        }
        uint32_t id = head.ReadUInt32();
        uint32_t numEntries = head.ReadUInt32();
        pos += 4;

        // A wild entry count would send pos far outside the file, and ReadSubArray
        // is not safe at an arbitrary address. Treat it as unparseable instead.
        if (numEntries > 0x4000 || pos > 0x200000) {
            return 0;
        }

        if (id == MM_CS_CAMERA_SPLINE) {
            // Headers run until a 0xFFFF marker; each is followed by two groups of
            // numEntries camera points and one group of misc points.
            uint32_t numHeaders = 0, totalCommands = 0;
            uint32_t p = pos + 4;
            for (;;) {
                auto hdr = ReadSubArray(buffer, segAddr + p, 8);
                if (hdr.GetLength() < 8) {
                    return 0;
                }
                uint16_t first = hdr.ReadUInt16();
                if (first == 0xFFFF) {
                    break;
                }
                numHeaders++;
                totalCommands += first;
                p += 8 + first * 0x0C * 2 + first * 0x08;
                if (p > 0x200000) {
                    return 0;
                }
                if (numHeaders > 0x400) {
                    return 0;
                }
            }
            pos += (8 + 8 * numHeaders + (totalCommands * 2) * 0x0C + totalCommands * 8 + 4) - 4;
        } else {
            pos += (8 + numEntries * MMEntryRawSize(id)) - 4;
        }
    }
    return pos + 8;
}

std::vector<char> CutsceneSerializer::SerializeMM(std::vector<uint8_t>& buffer, uint32_t segAddr) {
    uint32_t size = CalculateSizeMM(buffer, segAddr);
    if (size == 0) {
        return {};
    }

    auto r = ReadSubArray(buffer, segAddr, size);
    // ReadSubArray clamps to what the file actually holds. If it came up short the
    // walk below would read past the end, so bail rather than run off.
    if (r.GetLength() < size) {
        SPDLOG_WARN("MM cutscene at 0x{:X}: wanted 0x{:X} bytes, got 0x{:X}", segAddr, size, r.GetLength());
        return {};
    }

    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTCutscene, 0);

    uint32_t wordCountPos = w.GetStream()->GetLength();
    w.Write(static_cast<uint32_t>(0));
    uint32_t dataStartPos = w.GetStream()->GetLength();

    uint32_t numCommands = r.ReadUInt32();
    uint32_t endFrame = r.ReadUInt32();
    w.Write(numCommands);
    w.Write(endFrame);

    // These never fire on the retail rom, but a layout mistake in a command below
    // would otherwise walk off the end of the sub-array. Log and skip instead.
    const auto overrun = [&](const char* what, uint32_t i, uint32_t id, uint32_t need) {
        SPDLOG_ERROR("MM cutscene 0x{:X}: {} overran at command {}/{} (id {}), pos 0x{:X} + 0x{:X} > len 0x{:X}",
                     segAddr, what, i, numCommands, id, r.GetBaseAddress(), need, r.GetLength());
    };

    for (uint32_t i = 0; i < numCommands; i++) {
        if (r.GetBaseAddress() + 8 > r.GetLength()) {
            overrun("command header", i, 0, 8);
            return {};
        }
        uint32_t id = r.ReadUInt32();
        w.Write(id);

        if (id == MM_CS_CAMERA_SPLINE) {
            // The count here is a byte length, not an entry count.
            uint32_t byteLength = r.ReadUInt32();
            w.Write(byteLength);
            for (uint32_t guard = 0;; guard++) {
                if (guard > 0x400) {
                    SPDLOG_WARN("MM cutscene at 0x{:X}: spline list did not terminate", segAddr);
                    return {};
                }
                if (r.GetBaseAddress() + 8 > r.GetLength()) {
                    overrun("spline header", i, id, 8);
                    return {};
                }
                uint16_t numEntries = r.ReadUInt16();
                if (numEntries == 0xFFFF) {
                    // Footer: the remaining half word belongs to it.
                    r.ReadUInt16();
                    w.Write(static_cast<uint16_t>(0xFFFF));
                    w.Write(static_cast<uint16_t>(0x0004));
                    break;
                }
                uint16_t unused0 = r.ReadUInt16();
                uint16_t unused1 = r.ReadUInt16();
                uint16_t duration = r.ReadUInt16();
                w.Write(CS_CMD_HH(numEntries, unused0));
                w.Write(CS_CMD_HH(unused1, duration));

                for (uint32_t k = 0; k < numEntries * 2u; k++) {
                    uint8_t interpType = r.ReadUByte();
                    uint8_t weight = r.ReadUByte();
                    uint16_t dur = r.ReadUInt16();
                    uint16_t posX = r.ReadUInt16();
                    uint16_t posY = r.ReadUInt16();
                    uint16_t posZ = r.ReadUInt16();
                    uint16_t relTo = r.ReadUInt16();
                    w.Write(CS_CMD_BBH(interpType, weight, dur));
                    w.Write(CS_CMD_HH(posX, posY));
                    w.Write(CS_CMD_HH(posZ, relTo));
                }
                for (uint32_t k = 0; k < numEntries; k++) {
                    uint16_t u0 = r.ReadUInt16();
                    uint16_t roll = r.ReadUInt16();
                    uint16_t fov = r.ReadUInt16();
                    uint16_t u1 = r.ReadUInt16();
                    w.Write(CS_CMD_HH(u0, roll));
                    w.Write(CS_CMD_HH(fov, u1));
                }
            }
            continue;
        }

        uint32_t count = r.ReadUInt32();
        w.Write(count);

        if (r.GetBaseAddress() + count * MMEntryRawSize(id) > r.GetLength()) {
            overrun("entries", i, id, count * MMEntryRawSize(id));
            return {};
        }

        for (uint32_t e = 0; e < count; e++) {
            uint16_t base = r.ReadUInt16();
            uint16_t startFrame = r.ReadUInt16();
            uint16_t endF = r.ReadUInt16();

            if (MMIsActorCue(id)) {
                uint16_t rotX = r.ReadUInt16();
                uint16_t rotY = r.ReadUInt16();
                uint16_t rotZ = r.ReadUInt16();
                w.Write(CS_CMD_HH(base, startFrame));
                w.Write(CS_CMD_HH(endF, rotX));
                w.Write(CS_CMD_HH(rotY, rotZ));
                for (int j = 0; j < 6; j++) { // start/end positions
                    w.Write(r.ReadUInt32());
                }
                for (int j = 0; j < 3; j++) { // normal
                    w.Write(r.ReadUInt32());
                }
                continue;
            }

            switch (id) {
                case MM_CS_TEXT: {
                    uint16_t type = r.ReadUInt16();
                    uint16_t textId1 = r.ReadUInt16();
                    uint16_t textId2 = r.ReadUInt16();
                    w.Write(CS_CMD_HH(base, startFrame));
                    w.Write(CS_CMD_HH(endF, type));
                    w.Write(CS_CMD_HH(textId1, textId2));
                    break;
                }
                case MM_CS_TRANSITION_GENERAL: {
                    uint8_t unk06 = r.ReadUByte();
                    uint8_t unk07 = r.ReadUByte();
                    uint8_t unk08 = r.ReadUByte();
                    r.ReadUByte();
                    r.ReadUInt16();
                    w.Write(CS_CMD_HH(base, startFrame));
                    w.Write(CS_CMD_HBB(endF, unk06, unk07));
                    w.Write(CS_CMD_BBBB(unk08, 0, 0, 0));
                    break;
                }
                case MM_CS_FADE_OUT_SEQ: {
                    uint16_t pad = r.ReadUInt16();
                    r.ReadUInt32();
                    w.Write(CS_CMD_HH(base, startFrame));
                    w.Write(CS_CMD_HH(endF, pad));
                    w.Write(static_cast<uint32_t>(0));
                    break;
                }
                case MM_CS_TIME: {
                    uint8_t hour = r.ReadUByte();
                    uint8_t minute = r.ReadUByte();
                    r.ReadUInt32();
                    w.Write(CS_CMD_HH(base, startFrame));
                    w.Write(CS_CMD_HBB(endF, hour, minute));
                    w.Write(static_cast<uint32_t>(0));
                    break;
                }
                case MM_CS_RUMBLE: {
                    uint8_t intensity = r.ReadUByte();
                    uint8_t decayTimer = r.ReadUByte();
                    uint8_t decayStep = r.ReadUByte();
                    r.ReadUByte();
                    r.ReadUInt16();
                    w.Write(CS_CMD_HH(base, startFrame));
                    w.Write(CS_CMD_HBB(endF, intensity, decayTimer));
                    w.Write(CS_CMD_BBBB(decayStep, 0, 0, 0));
                    break;
                }
                default: {
                    uint16_t pad = r.ReadUInt16();
                    w.Write(CS_CMD_HH(base, startFrame));
                    w.Write(CS_CMD_HH(endF, pad));
                    break;
                }
            }
        }
    }

    w.Write(static_cast<uint32_t>(0xFFFFFFFF));
    w.Write(static_cast<uint32_t>(0));

    uint32_t endPos = w.GetStream()->GetLength();
    w.Seek(wordCountPos, LUS::SeekOffsetType::Start);
    w.Write(static_cast<uint32_t>((endPos - dataStartPos) / 4));
    w.Seek(endPos, LUS::SeekOffsetType::Start);

    std::stringstream ss;
    w.Finish(ss);
    auto str = ss.str();
    return std::vector<char>(str.begin(), str.end());
}

std::vector<char> CutsceneSerializer::Serialize(std::vector<uint8_t>& buffer, uint32_t segAddr) {
    if (Companion::Instance->IsMajorasMask()) {
        return SerializeMM(buffer, segAddr);
    }

    auto size = CalculateSize(buffer, segAddr);

    // Size of 0 means corrupt or empty cutscene data
    if (size == 0) return {};

    return Write(buffer, segAddr, size);
}

std::vector<char> CutsceneSerializer::Write(std::vector<uint8_t>& buffer, uint32_t segAddr, uint32_t size) {
    auto csReader = ReadSubArray(buffer, segAddr, size);

    // Use the actual bytes available (ReadSubArray may return less than requested)
    size = csReader.GetLength();

    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTCutscene, 0);

    // Write a placeholder for the cutscene word count.
    // We don't know the final count until all commands are serialized,
    // so we'll seek back and fill this in at the end.
    uint32_t wordCountPos = w.GetStream()->GetLength();
    w.Write(static_cast<uint32_t>(0));

    // Used to calculate word count at the end
    uint32_t dataStartPos = w.GetStream()->GetLength();

    uint32_t numCmds = csReader.ReadUInt32();
    uint32_t endFrame = csReader.ReadUInt32();
    w.Write(numCmds);
    w.Write(endFrame);

    for (uint32_t ci = 0; ci < numCmds && csReader.GetBaseAddress() + 8 <= size; ci++) {
        uint32_t cid = csReader.ReadUInt32();

        // End marker
        if (cid == 0xFFFFFFFF) break;

        // Skip unhandled commands (entryCount * 0x30 bytes)
        if (!IsHandledCmd(cid)) {
            uint32_t entryCount = csReader.ReadUInt32();
            uint32_t dataSize = entryCount * 0x30;
            if (csReader.GetBaseAddress() + dataSize <= size)
                csReader.Seek(csReader.GetBaseAddress() + dataSize, LUS::SeekOffsetType::Start);
            continue;
        }

        // Write command ID, then serialize its data
        w.Write(cid);

        if (IsCameraCmd(cid)) {
            WriteCameraCmd(csReader, w);
            continue;
        }

        if (IsSingleEntryCmd(cid)) {
            WriteSingleEntryCmd(csReader, w);
            continue;
        }

        // Default: entry-count-based commands
        WriteEntryCountCmd(cid, csReader, w);
    }

    // Write end marker
    w.Write(static_cast<uint32_t>(0xFFFFFFFF));
    w.Write(static_cast<uint32_t>(0));

    // Fill in the reserved word count now that we know the final size
    uint32_t dataEndPos = w.GetStream()->GetLength();
    w.Seek(wordCountPos, LUS::SeekOffsetType::Start);
    w.Write(static_cast<uint32_t>((dataEndPos - dataStartPos) / 4));
    w.Seek(dataEndPos, LUS::SeekOffsetType::Start);

    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    return std::vector<char>(str.begin(), str.end());
}

class OoTCutsceneData : public IParsedData {
public:
    std::vector<char> mBinary;
    OoTCutsceneData(std::vector<char> data) : mBinary(std::move(data)) {}
};

std::optional<std::shared_ptr<IParsedData>> OoTCutsceneFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto data = CutsceneSerializer::Serialize(buffer, GetSafeNode<uint32_t>(node, "offset"));
    if (data.empty()) {
        SPDLOG_WARN("OoTCutsceneFactory: Failed to serialize cutscene");
        return std::nullopt;
    }
    return std::make_shared<OoTCutsceneData>(std::move(data));
}

ExportResult OoTCutsceneBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto cs = std::static_pointer_cast<OoTCutsceneData>(raw);
    write.write(cs->mBinary.data(), cs->mBinary.size());
    return std::nullopt;
}

} // namespace OoT
