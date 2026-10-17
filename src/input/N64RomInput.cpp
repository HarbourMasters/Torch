#include "N64RomInput.h"

#include "spdlog/spdlog.h"

void N64RomInput::Open() {
    this->mCartridge = std::make_shared<N64::Cartridge>(this->mData);
    this->mCartridge->Initialize();
}

std::string N64RomInput::GetHash() {
    return this->mCartridge->GetHash();
}

std::string N64RomInput::GetTitle() {
    return this->mCartridge->GetGameTitle();
}

uint32_t N64RomInput::GetVersionCRC() {
    return this->mCartridge->GetCRC();
}

void N64RomInput::LogInfo() {
    SPDLOG_CRITICAL("Game: {}", this->mCartridge->GetGameTitle());
    SPDLOG_CRITICAL("CRC: {}", this->mCartridge->GetCRC());
    SPDLOG_CRITICAL("Version: {}", this->mCartridge->GetVersion());
    SPDLOG_CRITICAL("Country: [{}]", this->mCartridge->GetCountryCode());
    SPDLOG_CRITICAL("Hash: {}", this->mCartridge->GetHash());
}
