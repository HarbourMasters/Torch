#include "BankFactory.h"
#include "Companion.h"
#include "audio/samples_table.h"

void BankBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto bank = std::static_pointer_cast<BankData>(raw);

    WriteHeader(writer, LUS::ResourceType::Bank, 0);
    writer.Write(bank->mBankId);
    writer.Write((uint32_t) bank->mBank.insts.size());
    for (auto& instrument : bank->mBank.insts) {
        writer.Write((uint8_t) instrument.valid);
        if(!instrument.valid) continue;

        writer.Write((uint8_t) instrument.releaseRate);
        writer.Write((uint8_t) instrument.normalRangeLo);
        writer.Write((uint8_t) instrument.normalRangeHi);

        auto envelope = bank->mBank.envelopes[instrument.envelope];
        if(!envelope.entries.empty()){
            writer.Write((uint32_t) envelope.entries.size());
            for (auto& entry : envelope.entries) {
                writer.Write((uint16_t) entry.delay);
                writer.Write((uint16_t) entry.arg);
            }
        } else {
            writer.Write((uint32_t) 0);
        }

        uint32_t soundFlags = 0;
        bool hasLo = instrument.soundLo.has_value();
        bool hasMed = instrument.soundMed.has_value();
        bool hasHi = instrument.soundHi.has_value();
        if(hasLo){
            soundFlags |= (1 << 0);
        }
        if(hasMed){
            soundFlags |= (1 << 1);
        }
        if(hasHi){
            soundFlags |= (1 << 2);
        }

        writer.Write((uint32_t) soundFlags);
        if(hasLo){
            auto value = instrument.soundLo.value();
            uint32_t idx = AudioManager::Instance->get_index(bank->mBank.samples[value.offset]);
            writer.Write(std::string(bank->mSampleTable[idx]));
            writer.Write(value.tuning);
        }
        if(hasMed){
            auto value = instrument.soundMed.value();
            uint32_t idx = AudioManager::Instance->get_index(bank->mBank.samples[value.offset]);
            writer.Write(std::string(bank->mSampleTable[idx]));
            writer.Write(value.tuning);
        }
        if(hasHi){
            auto value = instrument.soundHi.value();
            uint32_t idx = AudioManager::Instance->get_index(bank->mBank.samples[value.offset]);
            writer.Write(std::string(bank->mSampleTable[idx]));
            writer.Write(value.tuning);
        }
    }

    writer.Write((uint32_t) bank->mBank.drums.size());
    for(auto &drum : bank->mBank.drums){
        writer.Write((uint8_t) drum.releaseRate);
        writer.Write((uint8_t) drum.pan);
        auto envelope = bank->mBank.envelopes[drum.envelope];
        if(!envelope.entries.empty()){
            writer.Write((uint32_t) envelope.entries.size());
            for(auto &entry : envelope.entries){
                writer.Write(entry.delay);
                writer.Write(entry.arg);
            }
        } else {
            writer.Write((uint32_t) 0);
        }

        uint32_t idx = AudioManager::Instance->get_index(bank->mBank.samples[drum.sound.offset]);
        std::string out = std::string(bank->mSampleTable[idx]);
        writer.Write(out);
        writer.Write(drum.sound.tuning);
    }

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> BankFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& data) {
    auto banks = AudioManager::Instance->get_banks();
    auto bankId = data["id"].as<uint32_t>();

    if(AudioManager::Instance == nullptr){
        throw std::runtime_error("AudioManager not initialized");
    }

    auto gSampleTable = Companion::Instance->GetCartridge()->GetCountry() == N64::CountryCode::Japan ? gJPSampleTable : gUSSampleTable;
    return std::make_shared<BankData>(banks[bankId], bankId, gSampleTable);
}