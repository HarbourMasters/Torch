#include "BankFactory.h"

#include <vector>
#include "audio/AudioManager.h"
#include "audio/samples_table.h"
#include "binarytools/BinaryReader.h"

bool BankFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {
    auto banks = AudioManager::Instance->get_banks();
    auto bankId = data["id"].as<uint32_t>();
    auto bank = banks[bankId];

    WRITE_HEADER(LUS::ResourceType::Bank, 0);

    WRITE_U32(bank.insts.size());
    for(auto &instrument : bank.insts){
        WRITE_U8(instrument.valid);
        if(!instrument.valid){
            continue;
        }
        WRITE_U8(instrument.releaseRate);
        WRITE_U8(instrument.normalRangeLo);
        WRITE_U8(instrument.normalRangeHi);

        auto envelope = bank.envelopes[instrument.envelope];
        if(!envelope.entries.empty()){
            WRITE_U32(envelope.entries.size());
            for(auto &entry : envelope.entries){
                writer->Write(entry.delay);
                writer->Write(entry.arg);
            }
        } else {
            WRITE_U32(0);
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

        WRITE_U32(soundFlags);
        if(hasLo){
            auto value = instrument.soundLo.value();
            uint32_t idx = AudioManager::Instance->get_index(bank.samples[value.offset]);
            writer->Write(std::string(gSampleTable[idx]));
            writer->Write(value.tuning);
        }
        if(hasMed){
            auto value = instrument.soundMed.value();
            uint32_t idx = AudioManager::Instance->get_index(bank.samples[value.offset]);
            writer->Write(std::string(gSampleTable[idx]));
            writer->Write(value.tuning);
        }
        if(hasHi){
            auto value = instrument.soundHi.value();
            uint32_t idx = AudioManager::Instance->get_index(bank.samples[value.offset]);
            writer->Write(std::string(gSampleTable[idx]));
            writer->Write(value.tuning);
        }

    }

    WRITE_U32(bank.drums.size());
    for(auto &drum : bank.drums){
        WRITE_U8(drum.releaseRate);
        WRITE_U8(drum.pan);
        auto envelope = bank.envelopes[drum.envelope];
        if(!envelope.entries.empty()){
            WRITE_U32(envelope.entries.size());
            for(auto &entry : envelope.entries){
                writer->Write(entry.delay);
                writer->Write(entry.arg);
            }
        } else {
            WRITE_U32(0);
        }

        uint32_t idx = AudioManager::Instance->get_index(bank.samples[drum.sound.offset]);
        std::string out = std::string(gSampleTable[idx]);
        writer->Write(out);
        writer->Write(drum.sound.tuning);
    }

    return true;
}