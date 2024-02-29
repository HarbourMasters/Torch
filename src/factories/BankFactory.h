#pragma once

#include <utility>

#include "BaseFactory.h"
#include "audio/AudioManager.h"

class BankData : public IParsedData {
public:
    Bank mBank;
    uint32_t mBankId;

    BankData(Bank bank, const uint32_t bankId) : mBank(std::move(bank)), mBankId(bankId) {}
};

class BankBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BankFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) override {
        return std::nullopt;
    }
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, BankBinaryExporter)
        };
    }
    bool SupportModdedAssets() override { return false; }
};