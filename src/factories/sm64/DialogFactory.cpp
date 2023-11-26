#include "DialogFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/MIODecoder.h"

void SM64::DialogBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto dialog = std::static_pointer_cast<DialogData>(raw);

    WriteHeader(writer, LUS::ResourceType::SDialog, 0);
    writer.Write((uint32_t) dialog->mUnused);
    writer.Write((int8_t) dialog->mLinesPerBox);
    writer.Write((int16_t) dialog->mLeftOffset);
    writer.Write((int16_t) dialog->mWidth);

    writer.Write((uint32_t) dialog->mText.size());
    writer.Write((char*) dialog->mText.data(), dialog->mText.size());
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SM64::DialogFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    VERIFY_ENTRY(node, "mio0", "yaml missing entry for mio0.\nEx. mio0: 0x10100")
    VERIFY_ENTRY(node, "offset", "yaml missing entry for offset.\nEx. offset: 0x100")

    auto offset = node["offset"].as<int32_t>();
    auto mio0 = node["mio0"].as<size_t>();

    auto decoded = MIO0Decoder::Decode(buffer, mio0);
    auto bytes = (uint8_t*) decoded.data();
    LUS::BinaryReader reader(bytes, decoded.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    auto unused = reader.ReadUInt32();
    auto linesPerBox = reader.ReadUByte();
    // Padding
    reader.Read(1);
    auto leftOffset = reader.ReadInt16();
    auto width = reader.ReadInt16();
    // Padding
    reader.Read(2);
    auto str = SEGMENT_OFFSET(reader.ReadInt32());
    std::vector<uint8_t> text;

    while(bytes[str] != 0xFF){
        auto c = bytes[str++];
        text.push_back(c);
    }
    text.push_back(0xFF);

    SPDLOG_INFO("Unused: {}", unused);
    SPDLOG_INFO("Lines per box: {}", linesPerBox);
    SPDLOG_INFO("Left offset: {}", leftOffset);
    SPDLOG_INFO("Width: {}", width);
    SPDLOG_INFO("Size: {}", text.size());

    return std::make_shared<DialogData>(unused, linesPerBox, leftOffset, width, text);
}