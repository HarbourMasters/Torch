#include "RawFactory.h"

void RawFactory::WriteHeader(LUS::BinaryWriter* writer, LUS::ResourceType resType, int32_t version){
    writer->Write((uint8_t)LUS::Endianness::Little); // 0x00
	writer->Write((uint8_t)0); // 0x01
	writer->Write((uint8_t)0); // 0x02
	writer->Write((uint8_t)0); // 0x03

	writer->Write((uint32_t) resType); // 0x04
	writer->Write((uint32_t) version); // 0x08
	writer->Write((uint64_t) 0xDEADBEEFDEADBEEF); // id, 0x0C
	writer->Write((uint32_t) 0); // 0x10
	writer->Write((uint64_t) 0); // ROM CRC, 0x14
	writer->Write((uint32_t) 0); // ROM Enum, 0x1C

	while (writer->GetBaseAddress() < 0x40)
		writer->Write((uint32_t)0); // To be used at a later date!
}