#include "OoTTextFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

struct OoTTextData : public IParsedData {
    std::vector<char> mBinary;
};

bool OoTTextFactory::IsEndOfMessageCode(uint8_t c) {
    return c == 0x02 // END
        || c == 0x07; // FADE2
}

unsigned int OoTTextFactory::GetTrailingBytes(uint8_t c) {
    switch (c) {
        case 0x05: // COLOR
        case 0x06: // SHIFT
        case 0x0C: // PERSISTENT
        case 0x0E: // FADE
        case 0x13: // ICON
        case 0x14: // SPEED
        case 0x1E: // BACKGROUND
            return 1;
        case 0x07: // FADE2 (also ends message)
        case 0x11: // HIGHSCORE
        case 0x12: // SOUND_EFFECT
            return 2;
        case 0x15: // THREE_CHOICE
            return 3;
        default:
            return 0;
    }
}

// Read a message string from raw data, handling OoT text control codes.
// Control codes have 0-3 trailing bytes that must be included in the output.
std::string OoTTextFactory::ReadMessageText(const uint8_t* rawData, size_t rawSize, uint32_t offset) {
    std::string msg;
    uint32_t ptr = offset;

    while (ptr < rawSize) {
        uint8_t byte = rawData[ptr];
        if (byte == 0x00) break;

        msg += (char)byte;
        ptr++;

        // Consume trailing bytes for control codes
        unsigned int trailing = GetTrailingBytes(byte);
        for (unsigned int i = 0; i < trailing && ptr < rawSize; i++) {
            uint8_t trailingByte = rawData[ptr];
            msg += (char)trailingByte;
            ptr++;
        }

        if (IsEndOfMessageCode(byte)) break;
    }

    return msg;
}

// NTSC: message offset is at bytes 4-7 of the entry
uint32_t OoTTextFactory::ReadMessageOffsetNTSC(const uint8_t* codeData, uint32_t entryPtr) {
    uint32_t raw = (codeData[entryPtr + 4] << 24) |
                   (codeData[entryPtr + 5] << 16) |
                   (codeData[entryPtr + 6] << 8) |
                    codeData[entryPtr + 7];

    // Mask off segment byte to get file-relative offset
    return raw & 0x00FFFFFF;
}

// PAL: message offset is a 4-byte entry in a separate language table
uint32_t OoTTextFactory::ReadMessageOffsetPAL(const uint8_t* codeData, uint32_t langPtr) {
    uint32_t raw = (codeData[langPtr]     << 24) |
                   (codeData[langPtr + 1] << 16) |
                   (codeData[langPtr + 2] << 8) |
                    codeData[langPtr + 3];

    // Mask off segment byte to get file-relative offset
    return raw & 0x00FFFFFF;
}

// Entry layout: [id_hi, id_lo, type_hi_nibble | ypos_lo_nibble, ...]
MessageEntry OoTTextFactory::ReadMessageMetadata(const uint8_t* codeData, uint32_t ptr) {
    MessageEntry entry;
    entry.id = (uint16_t)((codeData[ptr] << 8) | codeData[ptr + 1]);
    entry.textboxType = (codeData[ptr + 2] >> 4) & 0x0F;
    entry.textboxYPos = codeData[ptr + 2] & 0x0F;
    return entry;
}

std::vector<MessageEntry> OoTTextFactory::ParseMessagesNTSC(const DataChunk& code, uint32_t codeOffset,
                                                   const uint8_t* rawData, size_t rawSize) {
    std::vector<MessageEntry> messages;
    uint32_t currentPtr = codeOffset;

    while (true) {
        // Each message table entry is 8 bytes
        if (currentPtr + 8 > code.size) break;

        auto entry = ReadMessageMetadata(code.data, currentPtr);

        // NTSC stores the message offset inline at currentPtr + 4
        uint32_t msgOffset = ReadMessageOffsetNTSC(code.data, currentPtr);

        entry.msg = ReadMessageText(rawData, rawSize, msgOffset);
        messages.push_back(entry);

        // End of message table (0xFFFF) or staff credits (0xFFFC)
        if (entry.id == 0xFFFC || entry.id == 0xFFFF) break;

        currentPtr += 8;
    }

    return messages;
}

std::vector<MessageEntry> OoTTextFactory::ParseMessagesPAL(const DataChunk& code, uint32_t codeOffset,
                                                  uint32_t langPtr,
                                                  const uint8_t* rawData, size_t rawSize) {
    std::vector<MessageEntry> messages;
    uint32_t currentPtr = codeOffset;

    while (true) {
        // Each message table entry is 8 bytes
        if (currentPtr + 8 > code.size) break;

        auto entry = ReadMessageMetadata(code.data, currentPtr);

        // PAL stores message offsets in a separate language table (4 bytes each)
        uint32_t msgOffset = ReadMessageOffsetPAL(code.data, langPtr);

        entry.msg = ReadMessageText(rawData, rawSize, msgOffset);
        messages.push_back(entry);

        // End of message table (0xFFFF) or staff credits (0xFFFC)
        if (entry.id == 0xFFFC || entry.id == 0xFFFF) break;

        currentPtr += 8;
        langPtr += 4;
    }

    return messages;
}

std::optional<std::shared_ptr<IParsedData>> OoTTextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto codePhysStart = GetSafeNode<uint32_t>(node, "code_phys_start");
    auto codeOffset = GetSafeNode<uint32_t>(node, "code_offset");
    uint32_t langOffset = node["lang_offset"] ? node["lang_offset"].as<uint32_t>() : 0;
    bool isPalLang = (langOffset != 0 && langOffset != codeOffset);

    // Decompress code segment
    auto* codeChunk = Decompressor::Decode(buffer, codePhysStart, CompressionType::YAZ0);
    if (!codeChunk || !codeChunk->data) {
        SPDLOG_ERROR("OoTTextFactory: failed to decompress code segment");
        return std::nullopt;
    }

    // Get message data segment (uncompressed)
    auto msgSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(128);
    if (!msgSeg.has_value()) {
        SPDLOG_ERROR("OoTTextFactory: message data segment 128 not found");
        return std::nullopt;
    }

    const uint8_t* rawData = buffer.data() + msgSeg.value();
    size_t rawSize = buffer.size() - msgSeg.value();

    // Parse message entries
    auto messages = isPalLang
        ? ParseMessagesPAL(*codeChunk, codeOffset, langOffset, rawData, rawSize)
        : ParseMessagesNTSC(*codeChunk, codeOffset, rawData, rawSize);

    SPDLOG_INFO("OoTTextFactory: parsed {} messages", messages.size());

    // Build OTXT binary
    auto data = std::make_shared<OoTTextData>();
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTText, 0);

    w.Write(static_cast<uint32_t>(messages.size()));
    for (auto& m : messages) {
        w.Write(m.id);
        w.Write(m.textboxType);
        w.Write(m.textboxYPos);
        w.Write(m.msg);
    }

    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    data->mBinary = std::vector<char>(str.begin(), str.end());

    return data;
}

ExportResult OoTTextBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto data = std::static_pointer_cast<OoTTextData>(raw);
    write.write(data->mBinary.data(), data->mBinary.size());
    return std::nullopt;
}

}  // namespace OoT
