#include <iostream>
#include "SDialogFactory.h"

#include "spdlog/spdlog.h"
#include "utils/MIODecoder.h"
#include "binarytools/BinaryReader.h"

namespace fs = std::filesystem;

static size_t id = 0;

bool SDialogFactory::process(LUS::BinaryWriter* writer, YAML::Node& node, std::vector<uint8_t>& buffer) {

    WRITE_HEADER(LUS::ResourceType::SDialog, 0);

    auto offset = node["offset"].as<size_t>();
    auto mio0 = node["mio0"].as<size_t>();

    std::vector<uint8_t> text;
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

    WRITE_U32(unused);
    WRITE_I8(linesPerBox);
    WRITE_I16(leftOffset);
    WRITE_I16(width);

    WRITE_U32(text.size());
    WRITE_ARRAY(text.data(), text.size());

    id++;
	return true;
}