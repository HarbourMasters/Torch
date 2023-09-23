#include <iostream>
#include "SDialogFactory.h"

#include "utils/MIODecoder.h"
#include "binarytools/BinaryReader.h"

namespace fs = std::filesystem;

static size_t id = 0;

bool SDialogFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {

    WRITE_HEADER(LUS::ResourceType::SDialog, 0);

    auto offset = data["offset"].as<size_t>();
    auto mio0 = data["mio0"].as<size_t>();

    std::vector<uint8_t> text;
    auto decoded = MIO0Decoder::Decode(buffer, offset);
    auto bytes = (uint8_t*) decoded.data();
    LUS::BinaryReader reader(decoded.data(), decoded.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(mio0, LUS::SeekOffsetType::Start);

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

    std::cout << "unused: " << unused << std::endl;
    std::cout << "linesPerBox: " << (int) linesPerBox << std::endl;
    std::cout << "leftOffset: " << leftOffset << std::endl;
    std::cout << "width: " << width << std::endl;
    std::cout << "size: " << text.size() << std::endl;

    WRITE_U32(unused);
    WRITE_I8(linesPerBox);
    WRITE_I16(leftOffset);
    WRITE_I16(width);

    WRITE_U32(text.size());
    WRITE_ARRAY(text.data(), text.size());

    if(id == 85){
        int bp = 0;
    }

    id++;
	return true;
}

// seg2_dialog_table
// seg2_course_name_table
// seg2_act_name_table
