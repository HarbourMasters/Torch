#include "OoTTextFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

struct OoTTextData : public IParsedData {
    std::vector<char> mBinary;
};

std::optional<std::shared_ptr<IParsedData>> OoTTextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto codePhysStart = GetSafeNode<uint32_t>(node, "code_phys_start");
    auto codeOffset = GetSafeNode<uint32_t>(node, "code_offset");
    uint32_t langOffset = 0;
    bool isPalLang = false;
    if (node["lang_offset"]) {
        langOffset = node["lang_offset"].as<uint32_t>();
        if (langOffset != 0 && langOffset != codeOffset) {
            isPalLang = true;
        }
    }

    // Decompress code segment
    auto* codeChunk = Decompressor::Decode(buffer, codePhysStart, CompressionType::YAZ0);
    if (!codeChunk || !codeChunk->data) {
        SPDLOG_ERROR("OoTTextFactory: failed to decompress code segment");
        return std::nullopt;
    }
    const uint8_t* codeData = codeChunk->data;
    size_t codeSize = codeChunk->size;

    // Get message data segment (uncompressed)
    auto msgSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(128);
    if (!msgSeg.has_value()) {
        SPDLOG_ERROR("OoTTextFactory: message data segment 128 not found");
        return std::nullopt;
    }
    uint32_t msgBase = msgSeg.value();
    const uint8_t* rawData = buffer.data() + msgBase;
    size_t rawSize = buffer.size() - msgBase;

    // Parse message entries
    struct MessageEntry {
        uint16_t id;
        uint8_t textboxType;
        uint8_t textboxYPos;
        std::string msg;
    };
    std::vector<MessageEntry> messages;

    uint32_t currentPtr = codeOffset;
    uint32_t langPtr = isPalLang ? langOffset : codeOffset;

    while (true) {
        if (currentPtr + 8 > codeSize) break;

        MessageEntry entry;
        entry.id = (uint16_t)((codeData[currentPtr] << 8) | codeData[currentPtr + 1]);
        entry.textboxType = (codeData[currentPtr + 2] & 0xF0) >> 4;
        entry.textboxYPos = (codeData[currentPtr + 2] & 0x0F);

        // Get message text offset
        uint32_t msgOffset;
        if (isPalLang) {
            uint32_t raw = (codeData[langPtr] << 24) | (codeData[langPtr + 1] << 16) |
                           (codeData[langPtr + 2] << 8) | codeData[langPtr + 3];
            msgOffset = raw & 0x00FFFFFF;
        } else {
            uint32_t raw = (codeData[langPtr + 4] << 24) | (codeData[langPtr + 5] << 16) |
                           (codeData[langPtr + 6] << 8) | codeData[langPtr + 7];
            msgOffset = raw & 0x00FFFFFF;
        }

        // Parse message text with control codes
        uint32_t msgPtr = msgOffset;
        unsigned int extra = 0;
        bool stop = false;

        while (msgPtr < rawSize && ((!stop && rawData[msgPtr] != 0x00) || extra > 0)) {
            uint8_t c = rawData[msgPtr];
            entry.msg += (char)c;
            msgPtr++;

            if (extra == 0) {
                if (c == 0x02) {
                    stop = true;
                } else if (c == 0x05 || c == 0x13 || c == 0x0E || c == 0x0C ||
                           c == 0x1E || c == 0x06 || c == 0x14) {
                    extra = 1;
                } else if (c == 0x07) {
                    extra = 2;
                    stop = true;
                } else if (c == 0x12 || c == 0x11) {
                    extra = 2;
                } else if (c == 0x15) {
                    extra = 3;
                }
            } else {
                extra--;
            }
        }

        messages.push_back(entry);

        if (entry.id == 0xFFFC || entry.id == 0xFFFF)
            break;

        currentPtr += 8;
        langPtr += isPalLang ? 4 : 8;
    }

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
