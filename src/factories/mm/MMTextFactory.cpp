#include "MMTextFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"

#include <sstream>

namespace MM {

namespace {

constexpr uint8_t kStaffControlEnd = 0x02;
constexpr uint8_t kStaffControlColor = 0x05;
constexpr uint8_t kStaffControlShift = 0x06;
constexpr uint8_t kStaffControlTextId = 0x07;
constexpr uint8_t kStaffControlBoxBreakDelayed = 0x0C;
constexpr uint8_t kStaffControlFade = 0x0E;
constexpr uint8_t kStaffControlFade2 = 0x11;
constexpr uint8_t kStaffControlSfx = 0x12;
constexpr uint8_t kStaffControlItemIcon = 0x13;
constexpr uint8_t kStaffControlTextSpeed = 0x14;
constexpr uint8_t kStaffControlBackground = 0x15;
constexpr uint8_t kStaffControlHighscore = 0x1E;

constexpr uint8_t kNesControlShift = 0x14;
constexpr uint8_t kNesControlBoxBreakDelayed = 0x1B;
constexpr uint8_t kNesControlFade = 0x1C;
constexpr uint8_t kNesControlFadeSkippable = 0x1D;
constexpr uint8_t kNesControlSfx = 0x1E;
constexpr uint8_t kNesControlDelay = 0x1F;
constexpr uint8_t kNesControlEnd = 0xBF;

constexpr uint32_t kSegmentOffsetMask = 0x00FFFFFF;
constexpr uint8_t kStaffTextboxYPosMask = 0x0F;
constexpr uint16_t kMessageTableEndId = 0xFFFC;
constexpr uint16_t kMessageTableFinalId = 0xFFFF;

} // namespace

struct MMMessageEntry {
    uint16_t id;
    uint8_t textboxType;
    uint8_t textboxYPos;
    uint16_t icon;
    uint16_t nextMessageId;
    uint16_t firstItemCost;
    uint16_t secondItemCost;
    std::string message;
};

class MMTextData : public IParsedData {
public:
    std::vector<char> binary;
    explicit MMTextData(std::vector<char> data) : binary(std::move(data)) {}
};

static uint16_t ReadU16(const uint8_t* data, uint32_t offset) {
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

static uint32_t ReadU32(const uint8_t* data, uint32_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) | (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
}

static uint32_t TrailingBytes(uint8_t code, bool staff) {
    if (staff) {
        switch (code) {
            case kStaffControlColor:
            case kStaffControlShift:
            case kStaffControlFade:
            case kStaffControlItemIcon:
            case kStaffControlTextSpeed:
            case kStaffControlHighscore:
                return 1;
            case kStaffControlTextId:
            case kStaffControlBoxBreakDelayed:
            case kStaffControlFade2:
            case kStaffControlSfx:
                return 2;
            case kStaffControlBackground:
                return 3;
            default:
                return 0;
        }
    }

    switch (code) {
        case kNesControlShift:
            return 1;
        case kNesControlBoxBreakDelayed:
        case kNesControlFade:
        case kNesControlFadeSkippable:
        case kNesControlSfx:
        case kNesControlDelay:
            return 2;
        default:
            return 0;
    }
}

static std::string ReadMessage(const uint8_t* raw, size_t rawSize, uint32_t offset, uint8_t end, bool staff) {
    std::string message;
    while (offset < rawSize) {
        const uint8_t code = raw[offset++];
        message.push_back(static_cast<char>(code));
        const uint32_t trailing = TrailingBytes(code, staff);
        if (offset + trailing > rawSize) {
            return {};
        }
        for (uint32_t i = 0; i < trailing; ++i) {
            message.push_back(static_cast<char>(raw[offset++]));
        }
        if (code == end) {
            return message;
        }
    }
    return {};
}

std::optional<std::shared_ptr<IParsedData>> MMTextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const uint32_t codePhysStart = GetSafeNode<uint32_t>(node, "code_phys_start");
    const uint32_t codeOffset = GetSafeNode<uint32_t>(node, "code_offset");
    const uint32_t langOffset = GetSafeNode<uint32_t>(node, "lang_offset", 0);
    const std::string symbol = GetSafeNode<std::string>(node, "symbol");
    const bool staff = symbol == "staff_message_data_static";
    const bool japanese = symbol == "message_data_static_jp";

    if (japanese) {
        SPDLOG_ERROR("MMTextFactory: Japanese MM text is not implemented");
        return std::nullopt;
    }

    DataChunk decoded{};
    DataChunk* code = nullptr;
    const auto compression = Decompressor::GetCompressionType(buffer, codePhysStart);
    if (compression == CompressionType::None) {
        decoded = { buffer.data() + codePhysStart, buffer.size() - codePhysStart };
        code = &decoded;
    } else {
        code = Decompressor::Decode(buffer, codePhysStart, compression);
    }
    if (code == nullptr || code->data == nullptr || codeOffset + 8 > code->size) {
        return std::nullopt;
    }

    uint32_t current = codeOffset;
    uint32_t language = langOffset == 0 ? codeOffset : langOffset;
    std::vector<MMMessageEntry> messages;

    while (current + 8 <= code->size && language + 8 <= code->size) {
        MMMessageEntry entry{};
        entry.id = ReadU16(code->data, current);
        const uint32_t messageAddress = ReadU32(code->data, language + 4);
        const uint32_t messageOffset = messageAddress & kSegmentOffsetMask;
        const uint8_t segment = SEGMENT_NUMBER(messageAddress) == 0 ? Companion::Instance->GetCurrSegmentNumber()
                                                                    : SEGMENT_NUMBER(messageAddress);
        const auto messageSegment = Companion::Instance->GetFileOffsetFromSegmentedAddr(segment);
        if (!messageSegment.has_value() || messageSegment.value() >= buffer.size()) {
            return std::nullopt;
        }
        const uint8_t* raw = buffer.data() + messageSegment.value();
        const size_t rawSize = buffer.size() - messageSegment.value();

        if (staff) {
            entry.textboxType = 0;
            entry.textboxYPos = code->data[current + 2] & kStaffTextboxYPosMask;
            entry.message = ReadMessage(raw, rawSize, messageOffset, kStaffControlEnd, true);
        } else {
            if (messageOffset + 11 > rawSize) {
                return std::nullopt;
            }
            entry.textboxType = raw[messageOffset];
            entry.textboxYPos = raw[messageOffset + 1];
            entry.icon = raw[messageOffset + 2];
            entry.nextMessageId = ReadU16(raw, messageOffset + 3);
            entry.firstItemCost = ReadU16(raw, messageOffset + 5);
            entry.secondItemCost = ReadU16(raw, messageOffset + 7);
            entry.message = ReadMessage(raw, rawSize, messageOffset + 11, kNesControlEnd, false);
        }
        if (entry.message.empty()) {
            return std::nullopt;
        }
        messages.push_back(std::move(entry));
        if (messages.back().id == kMessageTableEndId || messages.back().id == kMessageTableFinalId) {
            break;
        }
        current += 8;
        language += langOffset == 0 ? 8 : 4;
    }

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::MMText, 0);
    writer.Write(static_cast<uint32_t>(messages.size()));
    for (const auto& message : messages) {
        writer.Write(message.id);
        writer.Write(message.textboxType);
        writer.Write(message.textboxYPos);
        writer.Write(message.icon);
        writer.Write(message.nextMessageId);
        writer.Write(message.firstItemCost);
        writer.Write(message.secondItemCost);
        writer.Write(message.message);
    }
    std::stringstream stream;
    writer.Finish(stream);
    const std::string serialized = stream.str();
    return std::make_shared<MMTextData>(std::vector<char>(serialized.begin(), serialized.end()));
}

ExportResult MMTextBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                          std::string&, YAML::Node&, std::string*) {
    const auto text = std::static_pointer_cast<MMTextData>(raw);
    write.write(text->binary.data(), text->binary.size());
    return std::nullopt;
}

} // namespace MM
