#pragma once

#include "BKEmitText.h"
#include "DialogFactory.h"

#include "spdlog/spdlog.h"
#include <binarytools/BinaryReader.h>
#include <binarytools/BinaryWriter.h>
#include <factories/BaseFactory.h>

#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace BK64 {

// Both question types end their entry list with this many answer options.
constexpr int kQuestionOptionCount = 3;

// One string's bytes as C source: printable characters quoted, control codes as hex.
inline void WriteEscapedChars(std::ostream& write, const std::string& str) {
    for (auto& c : str) {
        if (c < ' ') {
            write << ", 0x" << FORMAT_HEX((uint32_t)c, 2);
        } else if (c == '\'') {
            write << ", \'\\" << c << "\'";
        } else {
            write << ", \'" << c << "\'";
        }
    }
}

// A counted run of entries as C source: the count, then `cmd, length, bytes...` per entry.
inline void WriteDialogStringArray(std::ostream& write, const std::vector<DialogString>& entries) {
    write << fourSpaceTab << entries.size() << ",\n";
    for (const auto& [cmd, str] : entries) {
        write << fourSpaceTab << "0x" << FORMAT_HEX((uint32_t)cmd, 2) << ", " << str.length();
        WriteEscapedChars(write, str);
        write << ",\n";
    }
}

// The same run in the binary resource: u32 count, then cmd + u32 length + raw bytes.
inline void WriteDialogStrings(LUS::BinaryWriter& writer, const std::vector<DialogString>& entries) {
    writer.Write((uint32_t)entries.size());
    for (const auto& entry : entries) {
        writer.Write(entry.cmd);
        writer.Write((uint32_t)entry.str.length());
        // [port] Write(string) would prefix the length twice
        writer.Write((char*)entry.str.data(), entry.str.size());
    }
}

// The same run as a modding-yaml sequence of [cmd, "text"] pairs.
inline void EmitDialogStringSeq(YAML::Emitter& out, const std::vector<DialogString>& entries) {
    out << YAML::BeginSeq;
    for (const auto& [cmd, str] : entries) {
        out << YAML::Flow;
        out << YAML::BeginSeq;
        out << YAML_HEX((uint32_t)cmd);
        EmitText(out, str);
        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;
}

// Does the blob still hold `count` more bytes?
inline bool HasBytes(LUS::BinaryReader& reader, size_t count) {
    return reader.GetBaseAddress() + count <= reader.GetLength();
}

inline std::vector<DialogString> ReadDialogStrings(LUS::BinaryReader& reader, int count, const std::string& symbol,
                                                   const char* kind) {
    std::vector<DialogString> entries;
    for (int i = 0; i < count; i++) {
        if (!HasBytes(reader, 2)) {
            SPDLOG_WARN("[BK64] {} {}: entry {} of {} runs past the end of the blob; dropping the rest.", kind, symbol,
                        i + 1, count);
            break;
        }

        DialogString entry;
        entry.cmd = reader.ReadUByte();
        const auto strLen = reader.ReadUByte();

        if (!HasBytes(reader, strLen)) {
            SPDLOG_WARN("[BK64] {} {}: entry {} of {} claims {} bytes the blob doesn't hold; dropping the rest.", kind,
                        symbol, i + 1, count, strLen);
            break;
        }

        entry.str = reader.ReadString(strLen);
        entries.push_back(entry);
    }
    return entries;
}

// Read back a [cmd, "text"] sequence from modding yaml. Strings are re-terminated
// because the game reads them as C strings.
inline std::vector<DialogString> ReadModdingDialogSeq(const YAML::Node& seq) {
    std::vector<DialogString> entries;
    for (YAML::const_iterator it = seq.begin(); it != seq.end(); ++it) {
        DialogString entry;
        entry.cmd = (*it)[0].as<uint32_t>();
        entry.str = DecodeText((*it)[1].as<std::string>());
        entry.str += '\0';
        entries.push_back(entry);
    }
    return entries;
}

// Parse a modding-yaml buffer and return the asset's value node (the map under its symbol).
inline std::optional<YAML::Node> LoadModdingRoot(const std::vector<uint8_t>& buffer) {
    try {
        std::string text((char*)buffer.data(), buffer.size());
        YAML::Node assetNode = YAML::Load(text.c_str());
        return assetNode.begin()->second;
    } catch (YAML::ParserException& e) {
        SPDLOG_ERROR("Failed to parse message data: {}", e.what());
        SPDLOG_ERROR("{}", (char*)buffer.data());
        return std::nullopt;
    }
}

// Both question types share a container. Header byte 0 is the language count, followed
// by one LE u16 start offset per language. US has a single language, so its offset table
// is the lone `05 00` and the question begins right after it; PAL carries three, with
// English first at byte 9. Either way the reader ends up on the English block, and the
// returned vector holds the start offsets of the languages after it (empty on US/JP).
// Returns nullopt (and logs) when neither header matches.
inline std::optional<std::vector<uint16_t>> SeekQuestionBlock(LUS::BinaryReader& reader, const std::string& symbol,
                                                              const char* kind, const int8_t (&usHeader)[3],
                                                              const int8_t (&palHeader)[3]) {
    const auto header1 = reader.ReadInt8();
    const auto header2 = reader.ReadInt8();
    const auto header3 = reader.ReadInt8();

    if (header1 == usHeader[0] && header2 == usHeader[1] && header3 == usHeader[2]) {
        reader.ReadInt8(); // header4 (0x05)
        reader.ReadInt8(); // header5 (0x00)
        return std::vector<uint16_t>{};
    }

    if (header1 == palHeader[0] && header2 == palHeader[1] && header3 == palHeader[2]) {
        std::vector<uint16_t> extraOffsets;
        for (int i = 0; i < header1; i++) {
            const uint16_t offset = reader.ReadUByte() | (reader.ReadUByte() << 8);
            if (i > 0) {
                extraOffsets.push_back(offset); // index 0 is English, where the reader now sits
            }
        }
        return extraOffsets;
    }

    SPDLOG_ERROR("Invalid Header For BK64 {} {}: {:02X} {:02X} {:02X}", kind, symbol, header1, header2, header3);
    return std::nullopt;
}

} // namespace BK64
