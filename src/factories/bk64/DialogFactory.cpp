#include "DialogFactory.h"

#include "BKEmitText.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define DIALOG_HEADER_1 0x01
#define DIALOG_HEADER_2 0x03
#define DIALOG_HEADER_3 0x00
#define DIALOG_CMD_CLOSE 0x04
#define DIALOG_TERMINATOR_SIZE 3


namespace BK64 {

static void WriteLangBlock(LUS::BinaryWriter& writer, const std::vector<DialogString>& bottom,
                           const std::vector<DialogString>& top) {
    writer.Write((uint32_t)bottom.size());
    for (const auto& dialogString : bottom) {
        writer.Write(dialogString.cmd);
        writer.Write((uint32_t)dialogString.str.length());
        writer.Write((char*)dialogString.str.data(), dialogString.str.size());
    }

    writer.Write((uint32_t)top.size());
    for (const auto& dialogString : top) {
        writer.Write(dialogString.cmd);
        writer.Write((uint32_t)dialogString.str.length());
        writer.Write((char*)dialogString.str.data(), dialogString.str.size());
    }
}

ExportResult DialogCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                        YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto dialog = std::static_pointer_cast<DialogData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "u8 " << symbol << "[] = {\n";

    write << fourSpaceTab << "DIALOG_HEADER_1"
          << ", "
          << "DIALOG_HEADER_2"
          << ", "
          << "DIALOG_HEADER_3"
          << ",\n";
    write << fourSpaceTab << "/* Bottom Dialog */\n";
    write << fourSpaceTab << dialog->mBottom.size() << ",\n";
    for (const auto [cmd, str] : dialog->mBottom) {
        write << fourSpaceTab << "0x" << FORMAT_HEX((uint32_t)cmd, 2) << ", " << str.length();
        for (auto& c : str) {
            if (c < ' ') {
                write << ", 0x" << FORMAT_HEX((uint32_t)c, 2);
            } else if (c == '\'') {
                write << ", \'\\" << c << "\'";
            } else {
                write << ", \'" << c << "\'";
            }
        }
        write << ",\n";
    }
    write << fourSpaceTab << "/* Top Dialog */\n";
    write << fourSpaceTab << dialog->mTop.size() << ",\n";
    for (const auto [cmd, str] : dialog->mTop) {
        write << fourSpaceTab << "0x" << FORMAT_HEX((uint32_t)cmd, 2) << ", " << str.length();
        for (auto& c : str) {
            if (c < ' ') {
                write << ", 0x" << FORMAT_HEX((uint32_t)c, 2);
            } else if (c == '\'') {
                write << ", \'\\" << c << "\'";
            } else {
                write << ", \'" << c << "\'";
            }
        }
        write << ",\n";
    }

    write << "};\n\n";

    return offset;
}

ExportResult BK64::DialogBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto dialog = std::static_pointer_cast<DialogData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKDialog, 0);

    // 1 for US/JP, 3 for PAL (EN + FR + DE)
    uint32_t langCount = 1 + static_cast<uint32_t>(dialog->mExtraLangs.size());
    writer.Write(langCount);

    // English always goes first
    WriteLangBlock(writer, dialog->mBottom, dialog->mTop);

    // PAL only: French then German
    for (const auto& lang : dialog->mExtraLangs) {
        WriteLangBlock(writer, lang.bottom, lang.top);
    }

    writer.Finish(write);
    return std::nullopt;
}

ExportResult BK64::DialogModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                 std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto dialog = std::static_pointer_cast<DialogData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "Bottom";
    out << YAML::Value;

    out << YAML::BeginSeq;
    for (const auto [cmd, str] : dialog->mBottom) {
        out << YAML::Flow;
        out << YAML::BeginSeq;
        out << YAML_HEX((uint32_t)cmd);
        EmitText(out, str);
        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "Top";
    out << YAML::Value;

    out << YAML::BeginSeq;
    for (const auto [cmd, str] : dialog->mTop) {
        out << YAML::Flow;
        out << YAML::BeginSeq;
        out << YAML_HEX((uint32_t)cmd);
        EmitText(out, str);
        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;

    out << YAML::EndMap;
    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

// Give empty dialogs the terminator the game expects
static void EnsureTerminator(std::vector<DialogString>& box) {
    if (box.empty()) {
        box.push_back({ DIALOG_CMD_CLOSE, std::string(1, '\0') });
    }
}

static bool HasBytes(LUS::BinaryReader& reader, size_t count) {
    return reader.GetBaseAddress() + count <= reader.GetLength();
}

// Read one box's entries. A count byte the blob can't back would otherwise walk the reader off the
// end, and whatever memory follows gets baked into the o2r as dialog text. Returns false once the
// blob is exhausted so the caller stops rather than parsing the rest from nothing.
static bool ReadEntries(LUS::BinaryReader& reader, uint8_t count, std::vector<DialogString>& box,
                        const std::string& symbol, const char* boxName) {
    for (uint8_t i = 0; i < count; i++) {
        // cmd + length, then the string itself
        if (!HasBytes(reader, 2)) {
            SPDLOG_WARN("[BK64] Dialog {}: {} entry {} of {} runs past the end of the blob; dropping the rest.",
                        symbol, boxName, i + 1, count);
            return false;
        }

        DialogString dialogString;
        dialogString.cmd = reader.ReadUByte();
        auto strLen = reader.ReadUByte();

        if (!HasBytes(reader, strLen)) {
            SPDLOG_WARN("[BK64] Dialog {}: {} entry {} of {} claims {} bytes the blob doesn't hold; dropping the rest.",
                        symbol, boxName, i + 1, count, strLen);
            return false;
        }

        dialogString.str = reader.ReadString(strLen);
        box.push_back(dialogString);
    }
    return true;
}

// One language: bottom box strings, then top box strings.
static DialogLang ParseLangBlock(LUS::BinaryReader& reader, const std::string& symbol) {
    DialogLang lang;

    uint8_t bottomSize = 0;
    bool bottomComplete = false;

    if (HasBytes(reader, 1)) {
        bottomSize = reader.ReadUByte();
        bottomComplete = ReadEntries(reader, bottomSize, lang.bottom, symbol, "bottom");
    } else {
        SPDLOG_WARN("[BK64] Dialog {}: blob ends before the bottom box count.", symbol);
    }

    // Banjo's Backpack still writes the bottom box's terminator entry after a zero count. Consume
    // it or it gets read as topSize (0x04), inventing four junk top entries and running off the
    // blob; EnsureTerminator puts the entry back below. A real top box can't open with these bytes.
    if (bottomComplete && bottomSize == 0 && HasBytes(reader, DIALOG_TERMINATOR_SIZE)) {
        const uint32_t savedPos = reader.GetBaseAddress();
        const uint8_t b0 = reader.ReadUByte();
        const uint8_t b1 = reader.ReadUByte();
        const uint8_t b2 = reader.ReadUByte();
        if (b0 != DIALOG_CMD_CLOSE || b1 != 0x01 || b2 != 0x00) {
            reader.Seek(savedPos, LUS::SeekOffsetType::Start);
        }
    }

    // A truncated bottom means every offset past it is guesswork, so don't invent a top from it.
    if (bottomComplete && HasBytes(reader, 1)) {
        const uint8_t topSize = reader.ReadUByte();
        ReadEntries(reader, topSize, lang.top, symbol, "top");
    }

    EnsureTerminator(lang.bottom);
    EnsureTerminator(lang.top);

    return lang;
}

std::optional<std::shared_ptr<IParsedData>> DialogFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    auto header1 = reader.ReadInt8();
    auto header2 = reader.ReadInt8();
    auto header3 = reader.ReadInt8();

    if (header1 == DIALOG_HEADER_1 && header2 == DIALOG_HEADER_2 && header3 == DIALOG_HEADER_3) {
        // US/JP: 01 03 00, dialog data follows immediately
        auto lang = ParseLangBlock(reader, symbol);
        return std::make_shared<DialogData>(std::move(lang.bottom), std::move(lang.top));
    }

    if (header1 == 0x03 && header2 == 0x07 && header3 == 0x00) {
        // PAL: 03 07 00, then two LE u16 offsets (French, German), then the
        // EN/FR/DE blocks.
        uint16_t frenchOffset = reader.ReadUByte() | (reader.ReadUByte() << 8);
        uint16_t germanOffset = reader.ReadUByte() | (reader.ReadUByte() << 8);

        // EN sits right here at byte 7; FR and DE we seek to.
        auto english = ParseLangBlock(reader, symbol);

        reader.Seek(frenchOffset, LUS::SeekOffsetType::Start);
        auto french = ParseLangBlock(reader, symbol);

        reader.Seek(germanOffset, LUS::SeekOffsetType::Start);
        auto german = ParseLangBlock(reader, symbol);

        std::vector<DialogLang> extraLangs;
        extraLangs.push_back(std::move(french));
        extraLangs.push_back(std::move(german));

        return std::make_shared<DialogData>(std::move(english.bottom), std::move(english.top), std::move(extraLangs));
    }

    SPDLOG_ERROR("Invalid Header For BK64 Dialog {}: {:02X} {:02X} {:02X}", symbol, header1, header2, header3);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> DialogFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                         YAML::Node& node) {
    YAML::Node assetNode;

    try {
        std::string text((char*)buffer.data(), buffer.size());
        assetNode = YAML::Load(text.c_str());
    } catch (YAML::ParserException& e) {
        SPDLOG_ERROR("Failed to parse message data: {}", e.what());
        SPDLOG_ERROR("{}", (char*)buffer.data());
        return std::nullopt;
    }

    const auto info = assetNode.begin()->second;

    std::vector<DialogString> bottom;
    std::vector<DialogString> top;

    auto bottomNode = info["Bottom"];
    auto topNode = info["Top"];

    for (YAML::iterator it = bottomNode.begin(); it != bottomNode.end(); ++it) {
        DialogString dialogString;
        dialogString.cmd = (*it)[0].as<uint32_t>();
        dialogString.str = DecodeText((*it)[1].as<std::string>());
        dialogString.str += '\0';
        bottom.push_back(dialogString);
    }

    for (YAML::iterator it = topNode.begin(); it != topNode.end(); ++it) {
        DialogString dialogString;
        dialogString.cmd = (*it)[0].as<uint32_t>();
        dialogString.str = DecodeText((*it)[1].as<std::string>());
        dialogString.str += '\0';
        top.push_back(dialogString);
    }

    EnsureTerminator(bottom);
    EnsureTerminator(top);

    return std::make_shared<DialogData>(bottom, top);
}

} // namespace BK64
