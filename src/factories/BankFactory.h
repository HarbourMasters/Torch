#pragma once

#include "BaseFactory.h"
#include "audio/AudioManager.h"

class BankData : public IParsedData {
public:
    Bank mBank;
    uint32_t mBankId;
    const char** mSampleTable;

    BankData(Bank bank, uint32_t bankId, const char** sampleTable) : mBank(bank), mBankId(bankId), mSampleTable(sampleTable) {}
};

class BankBinaryExporter : public BaseExporter {
    void Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BankFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, BankBinaryExporter)
        };
    }
};