#include "AnimFactory.h"

#include <iostream>
#include <filesystem>
#include "Companion.h"
#include "utils/MIODecoder.h"
#include "binarytools/BinaryReader.h"

namespace fs = std::filesystem;

void WriteAnimationHeader(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
    auto offset = data["offset"].as<uint32_t>();
    auto size = data["size"].as<uint32_t>();

	LUS::BinaryReader reader((char*) buffer.data() + offset, size);
	reader.SetEndianness(LUS::Endianness::Big);

	auto flags = reader.ReadInt16();
	auto animYTransDivisor = reader.ReadInt16();
	auto startFrame = reader.ReadInt16();
	auto loopStart = reader.ReadInt16();
	auto loopEnd = reader.ReadInt16();
	auto unusedBoneCount = reader.ReadInt16();
	reader.ReadUInt32();
	reader.ReadUInt32();
	auto length = reader.ReadUInt32();

	WRITE_I16(flags);
	WRITE_I16(animYTransDivisor);
	WRITE_I16(startFrame);
	WRITE_I16(loopStart);
	WRITE_I16(loopEnd);
	WRITE_I16(unusedBoneCount);
	WRITE_U64(length);
	reader.Close();
}

void WriteAnimationIndex(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
    auto offset = data["offset"].as<uint32_t>();
    auto size = data["size"].as<uint32_t>();

    LUS::BinaryReader reader((char*) buffer.data() + offset, size);
	reader.SetEndianness(LUS::Endianness::Big);
	size_t entries = size / sizeof(uint16_t);
	WRITE_U32(entries);
	for(size_t id = 0; id < entries; id++) {
		WRITE_I16(reader.ReadInt16());
	}
	reader.Close();
}

void WriteAnimationValues(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
    auto offset = data["offset"].as<uint32_t>();
    auto size = data["size"].as<uint32_t>();

    LUS::BinaryReader reader((char*) buffer.data() + offset, size);
	reader.SetEndianness(LUS::Endianness::Big);
	size_t entries = size / sizeof(uint16_t);
	WRITE_U32(entries);
	for(size_t id = 0; id < entries; id++) {
		WRITE_U16(reader.ReadUInt16());
	}
	reader.Close();
}

bool AnimFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
	WRITE_HEADER(LUS::ResourceType::Anim, 0);

    auto header = data["header"];
    auto values = data["values"];
    auto index = data["indices"];

	WriteAnimationHeader(writer, header, buffer);
	WriteAnimationIndex(writer, index, buffer);
	WriteAnimationValues(writer, values, buffer);

	return true;
}
