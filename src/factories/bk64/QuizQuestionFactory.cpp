#include "QuizQuestionFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define QUIZ_QUESTION_HEADER_1 0x01
#define QUIZ_QUESTION_HEADER_2 0x01
#define QUIZ_QUESTION_HEADER_3 0x02
#define QUIZ_QUESTION_HEADER_4 0x05
#define QUIZ_QUESTION_HEADER_5 0x00

#define FORMAT_HEX(x, w) \
    std::hex << std::uppercase << std::setfill('0') << std::setw(w) << x << std::nouppercase << std::dec
#define YAML_HEX(num) YAML::Hex << (num) << YAML::Dec

namespace BK64 {

ExportResult QuizQuestionHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    return std::nullopt;
}

ExportResult QuizQuestionCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto quizQuestion = std::static_pointer_cast<QuizQuestionData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "u8 " << symbol << "[] = {\n";

    write << fourSpaceTab << "QUIZ_QUESTION_HEADER_1"
          << ", "
          << "QUIZ_QUESTION_HEADER_2"
          << ", "
          << "QUIZ_QUESTION_HEADER_3"
          << ", "
          << "QUIZ_QUESTION_HEADER_4"
          << ", "
          << "QUIZ_QUESTION_HEADER_5"
          << ",\n";
    write << fourSpaceTab << "/* QuizQuestion */\n";
    write << fourSpaceTab << quizQuestion->mText.size() << ",\n";
    for (const auto [cmd, str] : quizQuestion->mText) {
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
    write << fourSpaceTab << "/* Options */\n";
    write << fourSpaceTab << quizQuestion->mOptions.size() << ",\n";
    for (const auto [cmd, str] : quizQuestion->mOptions) {
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

ExportResult BK64::QuizQuestionBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                      std::string& entryName, YAML::Node& node,
                                                      std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto quizQuestion = std::static_pointer_cast<QuizQuestionData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKQuizQuestion, 0);

    writer.Write((uint32_t)quizQuestion->mText.size());
    for (const auto& dialogString : quizQuestion->mText) {
        writer.Write(dialogString.cmd);
        writer.Write((uint32_t)dialogString.str.length());
        writer.Write((char*)dialogString.str.data(),
                     dialogString.str.size()); // [port] Write(string) would prefix the length twice
    }

    writer.Write((uint32_t)quizQuestion->mOptions.size());
    for (const auto& optionString : quizQuestion->mOptions) {
        writer.Write(optionString.cmd);
        writer.Write((uint32_t)optionString.str.length());
        writer.Write((char*)optionString.str.data(),
                     optionString.str.size()); // [port] Write(string) would prefix the length twice
    }

    writer.Finish(write);
    return std::nullopt;
}

ExportResult BK64::QuizQuestionModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                       std::string& entryName, YAML::Node& node,
                                                       std::string* replacement) {
    const auto quizQuestion = std::static_pointer_cast<QuizQuestionData>(raw);
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

    out << YAML::BeginSeq;
    for (const auto [cmd, str] : quizQuestion->mText) {
        out << YAML::Flow;
        out << YAML::BeginSeq;
        out << YAML_HEX((uint32_t)cmd);
        out << str.c_str();
        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "Options";
    out << YAML::Value;

    out << YAML::BeginSeq;
    for (const auto [cmd, str] : quizQuestion->mOptions) {
        out << YAML::Flow;
        out << YAML::BeginSeq;
        out << YAML_HEX((uint32_t)cmd);
        out << str.c_str();
        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;

    out << YAML::EndMap;
    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

// The quiz data itself. Same layout for US and for PAL English.
static std::shared_ptr<QuizQuestionData> ParseQuizBlock(LUS::BinaryReader& reader) {
    std::vector<DialogString> text;
    std::vector<DialogString> options;

    auto textSize = reader.ReadUByte();

    for (uint8_t i = 0; i < textSize - 3; i++) {
        DialogString dialogString;
        dialogString.cmd = reader.ReadUByte();
        auto strLen = reader.ReadUByte();
        dialogString.str = reader.ReadString(strLen);
        text.push_back(dialogString);
    }

    for (uint8_t i = textSize - 3; i < textSize; i++) {
        DialogString optionString;
        optionString.cmd = reader.ReadUByte();
        auto strLen = reader.ReadUByte();
        optionString.str = reader.ReadString(strLen);
        options.push_back(optionString);
    }

    return std::make_shared<QuizQuestionData>(text, options);
}

std::optional<std::shared_ptr<IParsedData>> QuizQuestionFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    auto header1 = reader.ReadInt8();
    auto header2 = reader.ReadInt8();
    auto header3 = reader.ReadInt8();

    if (header1 == QUIZ_QUESTION_HEADER_1 && header2 == QUIZ_QUESTION_HEADER_2 && header3 == QUIZ_QUESTION_HEADER_3) {
        // US: 01 01 02 05 00, then quiz data
        reader.ReadInt8(); // header4 (0x05)
        reader.ReadInt8(); // header5 (0x00)
        return ParseQuizBlock(reader);
    }

    if (header1 == 0x03 && header2 == 0x01 && header3 == 0x02) {
        // PAL: 03 01 02, then 3 x LE u16 offsets (EN/FR/DE start positions).
        // The English block lives at the first of those offsets.
        uint16_t enOffset = reader.ReadUByte() | (reader.ReadUByte() << 8);
        reader.ReadUByte();
        reader.ReadUByte(); // skip FR offset
        reader.ReadUByte();
        reader.ReadUByte(); // skip DE offset
        // We're now at byte 9 = enOffset, so just read the English block.
        return ParseQuizBlock(reader);
    }

    SPDLOG_ERROR("Invalid Header For BK64 QuizQuestion {}: {:02X} {:02X} {:02X}", symbol, header1, header2, header3);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> QuizQuestionFactory::parse_modding(std::vector<uint8_t>& buffer,
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

    std::vector<DialogString> text;
    std::vector<DialogString> options;

    auto textNode = info["Text"];
    auto optionsNode = info["Options"];

    for (YAML::iterator it = textNode.begin(); it != textNode.end(); ++it) {
        DialogString dialogString;
        dialogString.cmd = (*it)[0].as<uint32_t>();
        dialogString.str = (*it)[1].as<std::string>();
        dialogString.str += '\0';
        text.push_back(dialogString);
    }

    uint32_t i = 0;
    for (YAML::iterator it = optionsNode.begin(); it != optionsNode.end(); ++it) {
        if (i >= 3) {
            SPDLOG_WARN("BK64 QuizQuestion: Only 3 Options Allowed; extra options ignored");
            break;
        }
        DialogString optionString;
        optionString.cmd = (*it)[0].as<uint32_t>();
        optionString.str = (*it)[1].as<std::string>();
        optionString.str += '\0';
        options.push_back(optionString);
        i++;
    }

    if (i != 3) {
        throw std::runtime_error("BK64 QuizQuestion: Requires Exactly 3 Options");
    }

    return std::make_shared<QuizQuestionData>(text, options);
}

} // namespace BK64
