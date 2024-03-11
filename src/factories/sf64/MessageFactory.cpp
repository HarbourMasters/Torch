#include "MessageFactory.h"

#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

#define END_CODE 0x0

std::vector<std::string> gCharCodeEnums = {
    "END", "NWL", "NP2", "NP3", "NP4", "NP5", "NP6", "NP7",
    "PRI0", "PRI1", "PRI2", "PRI3", "SPC", "HSP", "QSP", "NXT",
    "CLF", "CUP", "CRT", "CDN", "AUP", "ALF", "ADN", "ART",
    "_A", "_B", "_C", "_D", "_E", "_F", "_G", "_H",
    "_I", "_J", "_K", "_L", "_M", "_N", "_O", "_P",
    "_Q", "_R", "_S", "_T", "_U", "_V", "_W", "_X",
    "_Y", "_Z", "_a", "_b", "_c", "_d", "_e", "_f",
    "_g", "_h", "_i", "_j", "_k", "_l", "_m", "_n",
    "_o", "_p", "_q", "_r", "_s", "_t", "_u", "_v",
    "_w", "_x", "_y", "_z", "EXM", "QST", "DSH", "CMA",
    "PRD", "_0", "_1", "_2", "_3", "_4", "_5", "_6",
    "_7", "_8", "_9", "APS", "LPR", "RPR", "CLN", "PIP",
};

void SF64::MessageHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern Vtx " << symbol << "[];\n";
}

void SF64::MessageCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto message = std::static_pointer_cast<MessageData>(raw)->mMessage;
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    if (IS_SEGMENTED(offset)) {
        offset = SEGMENT_OFFSET(offset);
    }

    if (Companion::Instance->IsDebug()) {
        if (IS_SEGMENTED(offset)) {
            offset = SEGMENT_OFFSET(offset);
        }
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

    write << "u16 " << symbol << "[] = {\n" << fourSpaceTab;
    for (int i = 0; i < message.size(); ++i) {
        auto m = message[i];
        if(m == 0){
            write << "\n" << fourSpaceTab;
        }

        write << gCharCodeEnums[m] << ", ";
    }

    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// count: " << std::to_string(message.size()) << " chars\n";
        write << "// 0x" << std::hex << std::uppercase << (offset + (message.size() * sizeof(uint16_t))) << "\n";
    }

    write << "\n";
}

void SF64::MessageBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    const auto data = std::static_pointer_cast<MessageData>(raw);

    WriteHeader(writer, LUS::ResourceType::Message, 0);

    writer.Write(static_cast<uint32_t>(data->mMessage.size()));
    for(auto m : data->mMessage) {
        writer.Write(m);
    }
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SF64::MessageFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    std::vector<uint8_t> message;

    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    size_t idx = 0;
    while(segment.data[idx] != END_CODE){
        auto c = segment.data[idx++];
        message.push_back(c);
    }
    message.push_back(END_CODE);

    return std::make_shared<MessageData>(message);
}