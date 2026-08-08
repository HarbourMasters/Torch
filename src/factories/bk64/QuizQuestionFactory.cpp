#include "QuizQuestionFactory.h"

#include "BKDialogShared.h"
#include "BKEmitText.h"

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

namespace BK64 {

namespace {

constexpr int8_t kUsHeader[3] = { QUIZ_QUESTION_HEADER_1, QUIZ_QUESTION_HEADER_2, QUIZ_QUESTION_HEADER_3 };
constexpr int8_t kPalHeader[3] = { 0x03, 0x01, 0x02 };

// One language's quiz data. Same layout for every language. The last three entries are
// the answer options; their first two bytes are a fixed control pair the game expects,
// so they stay part of the string.
QuizQuestionLang ParseQuizBlock(LUS::BinaryReader& reader, const std::string& symbol) {
    const int textSize = reader.ReadUByte();
    auto text = ReadDialogStrings(reader, textSize - kQuestionOptionCount, symbol, "QuizQuestion");
    auto options = ReadDialogStrings(reader, textSize >= kQuestionOptionCount ? kQuestionOptionCount : 0, symbol,
                                     "QuizQuestion option");
    return { std::move(text), std::move(options) };
}

void WriteQuizLangBlock(LUS::BinaryWriter& writer, const std::vector<DialogString>& text,
                        const std::vector<DialogString>& options) {
    WriteDialogStrings(writer, text);
    WriteDialogStrings(writer, options);
}

} // namespace

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
    WriteDialogStringArray(write, quizQuestion->mText);
    write << fourSpaceTab << "/* Options */\n";
    WriteDialogStringArray(write, quizQuestion->mOptions);

    write << "};\n\n";

    return offset;
}

ExportResult BK64::QuizQuestionBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                      std::string& entryName, YAML::Node& node,
                                                      std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto quizQuestion = std::static_pointer_cast<QuizQuestionData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKQuizQuestion, 0);

    // 1 for US/JP, 3 for PAL (EN + FR + DE)
    const uint32_t langCount = 1 + static_cast<uint32_t>(quizQuestion->mExtraLangs.size());
    writer.Write(langCount);

    // English always goes first
    WriteQuizLangBlock(writer, quizQuestion->mText, quizQuestion->mOptions);

    // PAL only: French then German
    for (const auto& lang : quizQuestion->mExtraLangs) {
        WriteQuizLangBlock(writer, lang.text, lang.options);
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
    EmitDialogStringSeq(out, quizQuestion->mText);

    out << YAML::Key << "Options";
    out << YAML::Value;

    // Options expose their leading control pair as separate fields so a pack can
    // edit the text without touching them.
    out << YAML::BeginSeq;
    for (const auto& [cmd, str] : quizQuestion->mOptions) {
        out << YAML::Flow;
        out << YAML::BeginSeq;
        out << YAML_HEX((uint32_t)cmd);
        out << YAML_HEX((uint32_t)static_cast<uint8_t>(str[0]));
        out << YAML_HEX((uint32_t)static_cast<uint8_t>(str[1]));
        EmitText(out, str.substr(2));
        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;

    out << YAML::EndMap;
    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> QuizQuestionFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    const auto extraOffsets = SeekQuestionBlock(reader, symbol, "QuizQuestion", kUsHeader, kPalHeader);
    if (!extraOffsets.has_value()) {
        return std::nullopt;
    }

    auto english = ParseQuizBlock(reader, symbol);

    std::vector<QuizQuestionLang> extraLangs;
    for (const uint16_t offset : *extraOffsets) {
        reader.Seek(offset, LUS::SeekOffsetType::Start);
        extraLangs.push_back(ParseQuizBlock(reader, symbol));
    }

    return std::make_shared<QuizQuestionData>(std::move(english.text), std::move(english.options),
                                              std::move(extraLangs));
}

std::optional<std::shared_ptr<IParsedData>> QuizQuestionFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                              YAML::Node& node) {
    const auto info = LoadModdingRoot(buffer);
    if (!info.has_value()) {
        return std::nullopt;
    }

    auto text = ReadModdingDialogSeq((*info)["Text"]);

    std::vector<DialogString> options;
    auto optionsNode = (*info)["Options"];
    for (YAML::iterator it = optionsNode.begin(); it != optionsNode.end(); ++it) {
        if (options.size() >= 3) {
            SPDLOG_WARN("BK64 QuizQuestion: Only 3 Options Allowed; extra options ignored");
            break;
        }
        if ((*it).size() != 4) {
            throw std::runtime_error("BK64 QuizQuestion: option must be [cmd, 0xfd, 0x6c, \"text\"]");
        }
        DialogString optionString;
        optionString.cmd = (*it)[0].as<uint32_t>();
        optionString.str.push_back(static_cast<char>((*it)[1].as<uint32_t>()));
        optionString.str.push_back(static_cast<char>((*it)[2].as<uint32_t>()));
        optionString.str += DecodeText((*it)[3].as<std::string>());
        optionString.str += '\0';
        options.push_back(optionString);
    }

    if (options.size() != 3) {
        throw std::runtime_error("BK64 QuizQuestion: Requires Exactly 3 Options");
    }

    return std::make_shared<QuizQuestionData>(text, options);
}

} // namespace BK64
