#pragma once

#include "InputSource.h"
#include "n64/Cartridge.h"

class N64RomInput : public InputSource {
public:
    explicit N64RomInput(std::vector<uint8_t> data) : mData(std::move(data)) {}

    void Open() override;
    std::string GetHash() override;
    std::string GetTitle() override;
    uint32_t GetVersionCRC() override;
    std::vector<uint8_t>& GetImage() override { return mData; }
    void LogInfo() override;

    N64::Cartridge* GetCartridge() override { return mCartridge.get(); }
    std::shared_ptr<N64::Cartridge> GetSharedCartridge() { return mCartridge; }
    bool IsN64() const override { return true; }

private:
    std::vector<uint8_t> mData;
    std::shared_ptr<N64::Cartridge> mCartridge;
};
