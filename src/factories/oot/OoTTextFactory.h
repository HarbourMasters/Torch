#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include "utils/Decompressor.h"

namespace OoT {

// Handles OOT:TEXT type (OTXT 0x4F545854), message tables + per-language data.
// Reference: Shipwright's TextFactory.cpp for binary format.

class OoTTextBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

struct MessageEntry {
    uint16_t id;
    uint8_t textboxType;
    uint8_t textboxYPos;
    std::string msg;
};

class OoTTextFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& node) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTTextBinaryExporter)
        };
    }

private:
    static bool IsEndOfMessageCode(uint8_t c);
    static unsigned int GetTrailingBytes(uint8_t c);
    static std::string ReadMessageText(const uint8_t* rawData, size_t rawSize, uint32_t offset);
    static uint32_t ReadMessageOffsetNTSC(const uint8_t* codeData, uint32_t entryPtr);
    static uint32_t ReadMessageOffsetPAL(const uint8_t* codeData, uint32_t langPtr);
    static MessageEntry ReadMessageMetadata(const uint8_t* codeData, uint32_t ptr);
    static std::vector<MessageEntry> ParseMessagesNTSC(const DataChunk& code, uint32_t codeOffset,
                                                       const uint8_t* rawData, size_t rawSize);
    static std::vector<MessageEntry> ParseMessagesPAL(const DataChunk& code, uint32_t codeOffset,
                                                      uint32_t langPtr,
                                                      const uint8_t* rawData, size_t rawSize);
};

}  // namespace OoT

#endif
