#include "AudioFactory.h"

#include <vector>
#include "audio/AudioManager.h"
#include "audio/samples_table.h"
#include "binarytools/BinaryReader.h"

bool AudioFactory::process(LUS::BinaryWriter* writer, nlohmann::json& data, std::vector<uint8_t>& buffer) {
    auto metadata = data["offsets"];
	std::string path = data["path"];

	if(path.find("bank_sets") != std::string::npos){
		WRITE_HEADER(LUS::ResourceType::Blob, 0);
		char* raw = (char*) (buffer.data() + metadata[1]["us"][0]);
		WRITE_U32(metadata[0]);
		for(int i = 0; i < 0x23; i++){
			int16_t value;
			memcpy(&value, raw + (i * 2), 2);
			WRITE_I16(BSWAP16(value));
		}
		writer->Write(raw + 0x46, 0x5A);
        return true;
	}

    if(path.find("sound/banks") != std::string::npos){
        auto banks = AudioManager::Instance->get_banks();
        auto bankId = metadata["us"][0];
        auto bank = banks[bankId];

        if(bankId == 10){
            int bp = 1;
        }

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
                std::cout << "Envelope entries: " << envelope.entries.size() << std::endl;
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

    if(path.find("sound/sequences") != std::string::npos){

        if(!metadata[1].contains("us")){
            return false;
        }

        WRITE_HEADER(LUS::ResourceType::Sequence, 0);

        size_t size = metadata[0];
        auto offsets = metadata[1]["us"];

        std::vector<uint8_t> bank = metadata[1]["banks"];

        WRITE_U32(bank.size());
        WRITE_ARRAY(bank.data(), bank.size());

        WRITE_U32(size);
        WRITE_ARRAY(buffer.data() + (size_t)offsets[0], size);

        return true;
    }

    if(path.find("sound/samples") == std::string::npos || metadata.size() < 2 || !metadata[1].contains("us")){
        return false;
    }

    /*
     * HINT: Uncomment this to write the aiff to a file
     LUS::BinaryWriter aifcWriter;
     AudioManager::Instance->create_aifc(metadata[1]["us"][1], aifcWriter);
     std::vector<char> aifcData = aifcWriter.ToVector();
     aifcWriter.Close();
     write_aiff(aifcData, *writer);
    */

    WRITE_HEADER(LUS::ResourceType::Sample, 0);

    AudioBankSample entry = AudioManager::Instance->get_aifc(metadata[1]["us"][1]);

    // Write AdpcmLoop
    WRITE_U32(entry.loop.start);
    WRITE_U32(entry.loop.end);
    WRITE_I32(entry.loop.count);
    WRITE_U32(entry.loop.pad);

    if(entry.loop.state.has_value()){
        std::vector<int16_t> state = entry.loop.state.value();
        WRITE_U32(state.size());
        for(auto &value : state){
            writer->Write(value);
        }
    } else {
        WRITE_U32(0);
    }

    // Write AdpcmBook
    WRITE_I32(entry.book.order);
    WRITE_I32(entry.book.npredictors);

    WRITE_U32(entry.book.table.size());
    for(auto &value : entry.book.table){
        writer->Write(value);
    }

    // Write sample
    WRITE_I32(entry.data.size());
    for(auto &value : entry.data){
        WRITE_U8(value);
    }

    writer->Write(entry.name);

    return true;
}