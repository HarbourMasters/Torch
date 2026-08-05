#include "GruntyQuestionFactory.h"

#include "BKDialogShared.h"
#include "BKEmitText.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define GRUNTY_QUESTION_HEADER_1 0x01
#define GRUNTY_QUESTION_HEADER_2 0x03
#define GRUNTY_QUESTION_HEADER_3 0x00
#define GRUNTY_QUESTION_HEADER_4 0x05
#define GRUNTY_QUESTION_HEADER_5 0x00

namespace BK64 {

namespace {

constexpr int8_t kUsHeader[3] = { GRUNTY_QUESTION_HEADER_1, GRUNTY_QUESTION_HEADER_2, GRUNTY_QUESTION_HEADER_3 };
constexpr int8_t kPalHeader[3] = { 0x03, 0x03, 0x00 };

// The actual question data. Same shape whether we got here via US or PAL English.
// Unlike quiz options, these split their leading control pair out of the string.
std::shared_ptr<GruntyQuestionData> ParseGruntyBlock(LUS::BinaryReader& reader, const std::string& symbol) {
    const int textSize = reader.ReadUByte();
    auto text = ReadDialogStrings(reader, textSize - kQuestionOptionCount, symbol, "GruntyQuestion");

    std::vector<OptionString> options;
    if (textSize >= kQuestionOptionCount) {
        for (int i = 0; i < kQuestionOptionCount; i++) {
            // cmd + length + the two control bytes, then the string past them
            if (!HasBytes(reader, 4)) {
                SPDLOG_WARN("[BK64] GruntyQuestion {}: option {} of {} runs past the end of the blob; dropping "
                            "the rest.",
                            symbol, i + 1, kQuestionOptionCount);
                break;
            }

            OptionString optionString;
            optionString.cmd = reader.ReadUByte();
            const auto strLen = reader.ReadUByte();
            optionString.unk0 = reader.ReadUByte();
            optionString.unk1 = reader.ReadUByte();

            if (strLen < 2 || !HasBytes(reader, strLen - 2)) {
                SPDLOG_WARN("[BK64] GruntyQuestion {}: option {} of {} claims {} bytes the blob doesn't hold; "
                            "dropping the rest.",
                            symbol, i + 1, kQuestionOptionCount, strLen);
                break;
            }

            optionString.str = reader.ReadString(strLen - 2);
            options.push_back(optionString);
        }
    }

    return std::make_shared<GruntyQuestionData>(text, options);
}

} // namespace

ExportResult GruntyQuestionCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto gruntyQuestion = std::static_pointer_cast<GruntyQuestionData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "u8 " << symbol << "[] = {\n";

    write << fourSpaceTab << "GRUNTY_QUESTION_HEADER_1"
          << ", "
          << "GRUNTY_QUESTION_HEADER_2"
          << ", "
          << "GRUNTY_QUESTION_HEADER_3"
          << ", "
          << "GRUNTY_QUESTION_HEADER_4"
          << ", "
          << "GRUNTY_QUESTION_HEADER_5"
          << ",\n";
    write << fourSpaceTab << "/* GruntyQuestion */\n";
    WriteDialogStringArray(write, gruntyQuestion->mText);

    write << fourSpaceTab << "/* Options */\n";
    write << fourSpaceTab << gruntyQuestion->mOptions.size() << ",\n";
    for (const auto& [cmd, unk0, unk1, str] : gruntyQuestion->mOptions) {
        write << fourSpaceTab << "0x" << FORMAT_HEX((uint32_t)cmd, 2) << ", " << str.length();
        write << ", 0x" << FORMAT_HEX((uint32_t)unk0, 2) << ", 0x" << FORMAT_HEX((uint32_t)unk1, 2);
        WriteEscapedChars(write, str);
        write << ",\n";
    }

    write << "};\n\n";

    return offset;
}

ExportResult BK64::GruntyQuestionBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                        std::string& entryName, YAML::Node& node,
                                                        std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto gruntyQuestion = std::static_pointer_cast<GruntyQuestionData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKGruntyQuestion, 0);

    WriteDialogStrings(writer, gruntyQuestion->mText);

    writer.Write((uint32_t)gruntyQuestion->mOptions.size());
    for (const auto& optionString : gruntyQuestion->mOptions) {
        writer.Write(optionString.cmd);
        writer.Write(optionString.unk0);
        writer.Write(optionString.unk1);
        writer.Write((uint32_t)optionString.str.length());
        // [port] Write(string) would prefix the length twice
        writer.Write((char*)optionString.str.data(), optionString.str.size());
    }

    writer.Finish(write);
    return std::nullopt;
}

ExportResult BK64::GruntyQuestionModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                         std::string& entryName, YAML::Node& node,
                                                         std::string* replacement) {
    const auto gruntyQuestion = std::static_pointer_cast<GruntyQuestionData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "Text";
    out << YAML::Value;
    EmitDialogStringSeq(out, gruntyQuestion->mText);

    out << YAML::Key << "Options";
    out << YAML::Value;

    out << YAML::BeginSeq;
    for (const auto& [cmd, unk0, unk1, str] : gruntyQuestion->mOptions) {
        out << YAML::Flow;
        out << YAML::BeginSeq;
        out << YAML_HEX((uint32_t)cmd);
        out << YAML_HEX((uint32_t)unk0);
        out << YAML_HEX((uint32_t)unk1);
        EmitText(out, str);
        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;

    out << YAML::EndMap;
    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> GruntyQuestionFactory::parse(std::vector<uint8_t>& buffer,
                                                                        YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    if (!SeekQuestionBlock(reader, symbol, "GruntyQuestion", kUsHeader, kPalHeader)) {
        return std::nullopt;
    }

    return ParseGruntyBlock(reader, symbol);
}

std::optional<std::shared_ptr<IParsedData>> GruntyQuestionFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                                 YAML::Node& node) {
    const auto info = LoadModdingRoot(buffer);
    if (!info.has_value()) {
        return std::nullopt;
    }

    auto text = ReadModdingDialogSeq((*info)["Text"]);

    std::vector<OptionString> options;
    auto optionsNode = (*info)["Options"];
    for (YAML::iterator it = optionsNode.begin(); it != optionsNode.end(); ++it) {
        if (options.size() >= kQuestionOptionCount) {
            SPDLOG_WARN("BK64 GruntyQuestion: Only 3 Options Allowed; extra options ignored");
            break;
        }
        OptionString optionString;
        optionString.cmd = (*it)[0].as<uint32_t>();
        optionString.unk0 = (*it)[1].as<uint32_t>();
        optionString.unk1 = (*it)[2].as<uint32_t>();
        optionString.str = DecodeText((*it)[3].as<std::string>());
        optionString.str += '\0';
        options.push_back(optionString);
    }

    if (options.size() != kQuestionOptionCount) {
        throw std::runtime_error("BK64 GruntyQuestion: Requires Exactly 3 Options");
    }

    return std::make_shared<GruntyQuestionData>(text, options);
}

} // namespace BK64
