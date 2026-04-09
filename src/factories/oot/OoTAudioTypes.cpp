#ifdef OOT_SUPPORT

#include "OoTAudioTypes.h"

namespace OoT {

SafeAudioBankReader::SafeAudioBankReader(std::vector<uint8_t> data)
    : mData(std::move(data)), mReader((char*)mData.data(), mData.size()) {
    mReader.SetEndianness(Torch::Endianness::Big);
}

uint8_t SafeAudioBankReader::ReadU8(uint32_t offset) {
    if (offset >= mData.size()) return 0;
    mReader.Seek(offset, LUS::SeekOffsetType::Start);
    return mReader.ReadUByte();
}

uint32_t SafeAudioBankReader::ReadU32(uint32_t offset) {
    if (offset + 4 > mData.size()) return 0;
    mReader.Seek(offset, LUS::SeekOffsetType::Start);
    return mReader.ReadUInt32();
}

int16_t SafeAudioBankReader::ReadS16(uint32_t offset) {
    if (offset + 2 > mData.size()) return 0;
    mReader.Seek(offset, LUS::SeekOffsetType::Start);
    return mReader.ReadInt16();
}

float SafeAudioBankReader::ReadFloat(uint32_t offset) {
    if (offset + 4 > mData.size()) return 0.0f;
    mReader.Seek(offset, LUS::SeekOffsetType::Start);
    return mReader.ReadFloat();
}

} // namespace OoT

#endif
