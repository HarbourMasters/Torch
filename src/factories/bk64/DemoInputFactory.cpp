#include "DemoInputFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define YAML_HEX(num) YAML::Hex << (num) << YAML::Dec
#define FORMAT_HEX(x, w) \
    std::hex << std::uppercase << std::setfill('0') << std::setw(w) << x << std::nouppercase << std::dec

namespace BK64 {

ExportResult DemoInputHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    return std::nullopt;
}

ExportResult DemoInputCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto demoInput = std::static_pointer_cast<DemoInputData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "DemoFileHeader " << symbol << " = {\n";

    write << fourSpaceTab << demoInput->mInputs.size() * sizeof(ControllerInput) << ",\n";

    for (const auto& input : demoInput->mInputs) {
        write << fourSpaceTab << "{ ";
        write << (int32_t)input.stickX << ", " << (int32_t)input.stickY << ", 0x" << FORMAT_HEX(input.buttons, 4)
              << ", " << (uint32_t)input.frames << ", " << (uint32_t)input.unkFlag;
        write << " },\n";
    }

    write << "};\n\n";

    return offset;
}

ExportResult BK64::DemoInputBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                   std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto demoInput = std::static_pointer_cast<DemoInputData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKDemoInput, 0);

    writer.Write((uint32_t)demoInput->mInputs.size());
    for (const auto& input : demoInput->mInputs) {
        writer.Write(input.stickX);
        writer.Write(input.stickY);
        writer.Write(input.buttons);
        writer.Write(input.frames);
        writer.Write(input.unkFlag);
    }

    writer.Finish(write);
    return std::nullopt;
}

ExportResult BK64::DemoInputModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                    std::string& entryName, YAML::Node& node,
                                                    std::string* replacement) {
    const auto demoInput = std::static_pointer_cast<DemoInputData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "ControllerInputs" << YAML::Value;

    out << YAML::BeginSeq;
    for (const auto& input : demoInput->mInputs) {
        out << YAML::BeginMap;
        out << YAML::Key << "StickX" << YAML::Value << (int32_t)input.stickX;
        out << YAML::Key << "StickY" << YAML::Value << (int32_t)input.stickY;
        out << YAML::Key << "Buttons" << YAML::Value << YAML_HEX(input.buttons);
        out << YAML::Key << "Frames" << YAML::Value << (uint32_t)input.frames;
        out << YAML::Key << "UnkFlag" << YAML::Value << (uint32_t)input.unkFlag;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> DemoInputFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");

    SPDLOG_INFO("START SYMBOL {}", symbol);

    if (segment.size < 4) {
        return std::make_shared<DemoInputData>(std::vector<ControllerInput>());
    }

    auto size = reader.ReadUInt32();

    std::vector<ControllerInput> inputs;

    for (uint32_t i = 0; i < size / sizeof(ControllerInput); i++) {
        ControllerInput input;

        input.stickX = reader.ReadInt8();
        input.stickY = reader.ReadInt8();
        input.buttons = reader.ReadUInt16();
        input.frames = reader.ReadUByte();
        input.unkFlag = reader.ReadUByte();

        inputs.push_back(input);
    }

    return std::make_shared<DemoInputData>(inputs);
}

std::optional<std::shared_ptr<IParsedData>> DemoInputFactory::parse_modding(std::vector<uint8_t>& buffer,
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

    auto controllerInfo = info["ControllerInputs"];

    std::vector<ControllerInput> inputs;

    for (YAML::iterator it = controllerInfo.begin(); it != controllerInfo.end(); ++it) {
        ControllerInput input;

        auto inputInfo = *it;

        input.stickX = inputInfo["StickX"].as<int32_t>();
        input.stickY = inputInfo["StickY"].as<int32_t>();
        input.buttons = inputInfo["Buttons"].as<uint16_t>();
        input.frames = inputInfo["Frames"].as<uint32_t>();
        input.unkFlag = inputInfo["UnkFlag"].as<uint32_t>();

        inputs.push_back(input);
    }

    return std::make_shared<DemoInputData>(inputs);
}

} // namespace BK64
