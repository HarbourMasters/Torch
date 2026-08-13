#include "MMTextFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

// Majora's Mask message text. The layout, the control codes and their argument
// widths all come from ZAPDTR/ZAPD/ZTextMM.cpp (ZTextMM::ParseMM); the field
// order written out is OTRExporter/TextMMExporter.cpp.
//
// The message *table* lives in `code` and the message *text* in the resource's
// own file, so this needs both: code_phys_start/code_offset for the table, and
// segment 128 for the text.
//
// MM's format is not OoT's. The table entry is 8 bytes with the offset at +4
// (top byte segment, low 24 bits the offset), each message carries a 11-byte
// header of its own, the terminator is 0xBF rather than 0x02, and the control
// codes that take arguments are a different set.

namespace OoT {

struct MMTextData : public IParsedData {
    std::vector<char> mBinary;
};

namespace {

// ZAPD indexes its buffers without bounds checks. Reads past the end are not
// expected; returning 0 keeps a malformed table from running off the buffer
// instead of crashing, and matches ZAPD wherever the data is well formed.
uint8_t ReadU8(const uint8_t* data, size_t size, size_t at) {
    return at < size ? data[at] : 0;
}

uint16_t ReadU16BE(const uint8_t* data, size_t size, size_t at) {
    return static_cast<uint16_t>((ReadU8(data, size, at) << 8) | ReadU8(data, size, at + 1));
}

uint32_t ReadU32BE(const uint8_t* data, size_t size, size_t at) {
    return (static_cast<uint32_t>(ReadU16BE(data, size, at)) << 16) | ReadU16BE(data, size, at + 2);
}

// The staff credits messages have no header, end at 0x02, and use their own
// control codes. ZTextMM.cpp, the `staff_message_data_static` branch.
std::string ReadStaffMessage(const uint8_t* rawData, size_t rawSize, uint32_t msgPtr) {
    std::string msg;

    while (msgPtr < rawSize) {
        const uint8_t c = ReadU8(rawData, rawSize, msgPtr);
        msg += static_cast<char>(c);

        if (c == 0x02) { // END
            break;
        }

        unsigned int args = 0;
        switch (c) {
            case 0x05: // COLOR
            case 0x06: // SHIFT
            case 0x0E: // FADE
            case 0x13: // ITEM ICON
            case 0x14: // TEXT SPEED
            case 0x1E: // HIGHSCORE
                args = 1;
                break;
            case 0x07: // TEXTID
            case 0x0C: // BOX BREAK DELAY
            case 0x11: // FADE2
            case 0x12: // SFX
                args = 2;
                break;
            case 0x15: // BACKGROUND
                args = 3;
                break;
            default:
                break;
        }

        for (unsigned int i = 1; i <= args; i++) {
            msg += static_cast<char>(ReadU8(rawData, rawSize, msgPtr + i));
        }
        msgPtr += args + 1;
    }

    return msg;
}

// NES messages end at 0xBF. The 11-byte header has already been consumed by the
// caller, which is where msgPtr points.
std::string ReadNesMessage(const uint8_t* rawData, size_t rawSize, uint32_t msgPtr) {
    std::string msg;

    while (msgPtr < rawSize) {
        const uint8_t c = ReadU8(rawData, rawSize, msgPtr);
        msg += static_cast<char>(c);

        if (c == 0xBF) { // END
            break;
        }

        unsigned int args = 0;
        switch (c) {
            case 0x14: // SHIFT
                args = 1;
                break;
            case 0x1B: // BOX BREAK DELAY
            case 0x1C: // FADE
            case 0x1D: // FADE SKIPPABLE
            case 0x1E: // SFX
            case 0x1F: // DELAY
                args = 2;
                break;
            default:
                break;
        }

        for (unsigned int i = 1; i <= args; i++) {
            msg += static_cast<char>(ReadU8(rawData, rawSize, msgPtr + i));
        }
        msgPtr += args + 1;
    }

    return msg;
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> MMTextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto codePhysStart = GetSafeNode<uint32_t>(node, "code_phys_start");
    auto codeOffset = GetSafeNode<uint32_t>(node, "code_offset");
    const uint32_t langOffset = node["lang_offset"] ? node["lang_offset"].as<uint32_t>() : 0;

    // ZAPD keys the staff format off the file's name; so does this.
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const bool isStaff = symbol == "staff_message_data_static";

    DataChunk uncompressedChunk{};
    DataChunk* codeChunk;
    auto codeCompression = Decompressor::GetCompressionType(buffer, codePhysStart);
    if (codeCompression == CompressionType::None) {
        uncompressedChunk = { buffer.data() + codePhysStart, buffer.size() - codePhysStart };
        codeChunk = &uncompressedChunk;
    } else {
        codeChunk = Decompressor::Decode(buffer, codePhysStart, codeCompression);
    }
    if (!codeChunk || !codeChunk->data) {
        SPDLOG_ERROR("MMTextFactory: failed to decode code segment");
        return std::nullopt;
    }
    const uint8_t* codeData = codeChunk->data;
    const size_t codeSize = codeChunk->size;

    auto msgSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(128);
    if (!msgSeg.has_value()) {
        SPDLOG_ERROR("MMTextFactory: message data segment 128 not found");
        return std::nullopt;
    }
    const uint8_t* rawData = buffer.data() + msgSeg.value();
    const size_t rawSize = buffer.size() - msgSeg.value();

    uint32_t currentPtr = codeOffset;
    uint32_t langPtr = currentPtr;
    const bool isPalLang = (langOffset != 0 && langOffset != codeOffset);
    if (langOffset != 0) {
        langPtr = langOffset;
    }

    std::vector<MMMessageEntry> messages;
    while (currentPtr + 8 <= codeSize && langPtr + 8 <= codeSize) {
        MMMessageEntry entry;
        entry.id = ReadU16BE(codeData, codeSize, currentPtr);

        uint32_t msgPtr = ReadU32BE(codeData, codeSize, langPtr + 4) & 0x00FFFFFF;

        if (isStaff) {
            // ZAPD reads the packed type/position byte out of the table and then
            // immediately zeroes textboxType again -- but not textboxYPos, which
            // keeps the low nibble. Reproduced as written.
            const uint8_t typePos = ReadU8(codeData, codeSize, currentPtr + 2);
            entry.textboxType = (typePos & 0xF0) >> 4;
            entry.textboxYPos = typePos & 0x0F;
            entry.textboxType = 0;

            entry.msg = ReadStaffMessage(rawData, rawSize, msgPtr);
        } else {
            entry.textboxType = ReadU8(rawData, rawSize, msgPtr + 0);
            entry.textboxYPos = ReadU8(rawData, rawSize, msgPtr + 1);
            entry.icon = ReadU8(rawData, rawSize, msgPtr + 2);
            entry.nextMessageID = ReadU16BE(rawData, rawSize, msgPtr + 3);
            entry.firstItemCost = ReadU16BE(rawData, rawSize, msgPtr + 5);
            entry.secondItemCost = ReadU16BE(rawData, rawSize, msgPtr + 7);

            entry.msg = ReadNesMessage(rawData, rawSize, msgPtr + 11);
        }

        messages.push_back(std::move(entry));

        if (messages.back().id == 0xFFFC || messages.back().id == 0xFFFF) {
            break;
        }

        currentPtr += 8;
        langPtr += isPalLang ? 4 : 8;
    }

    SPDLOG_INFO("MMTextFactory: parsed {} messages for {}", messages.size(), symbol);

    auto data = std::make_shared<MMTextData>();
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::MMText, 0);

    w.Write(static_cast<uint32_t>(messages.size()));
    for (auto& m : messages) {
        w.Write(m.id);
        w.Write(m.textboxType);
        w.Write(m.textboxYPos);
        w.Write(m.icon);
        w.Write(m.nextMessageID);
        w.Write(m.firstItemCost);
        w.Write(m.secondItemCost);
        w.Write(m.msg);
    }

    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    data->mBinary = std::vector<char>(str.begin(), str.end());

    return data;
}

ExportResult MMTextBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                          std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto data = std::static_pointer_cast<MMTextData>(raw);
    write.write(data->mBinary.data(), data->mBinary.size());
    return std::nullopt;
}

} // namespace OoT
