#include "Cartridge.h"

#include "hj/sha1.h"
#include "binarytools/BinaryReader.h"

void N64::Cartridge::Initialize() {
    LUS::BinaryReader reader((char*) this->gRomData.data(), this->gRomData.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(0x20, LUS::SeekOffsetType::Start);
    this->gGameTitle = std::string(reader.ReadCString());
    reader.Seek(0x3E, LUS::SeekOffsetType::Start);
    uint8_t country = reader.ReadUByte();
    this->gVersion = reader.ReadUByte();
    this->gHash = Chocobo1::SHA1().addData(this->gRomData).finalize().toString();
    switch (country) {
        case 'J':
            this->gCountryCode = CountryCode::Japan;
            break;
        case 'E':
            this->gCountryCode = CountryCode::NorthAmerica;
            break;
        case 'P':
            this->gCountryCode = CountryCode::Europe;
            break;
        default:
            this->gCountryCode = CountryCode::Unknown;
            break;
    }
    reader.Close();
}

const std::string &N64::Cartridge::GetGameTitle() {
    return this->gGameTitle;
}

N64::CountryCode N64::Cartridge::GetCountry() {
    return this->gCountryCode;
}

uint8_t N64::Cartridge::GetVersion() const {
    return this->gVersion;
}

std::string N64::Cartridge::GetHash() {
    return this->gHash;
}

std::string N64::Cartridge::GetCountryCode() {
    switch (this->gCountryCode) {
        case CountryCode::Japan:
            return "jp";
        case CountryCode::NorthAmerica:
            return "us";
        case CountryCode::Europe:
            return "eu";
        default:
            return "unk";
    }
}
