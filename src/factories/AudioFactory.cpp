#include "AudioFactory.h"

#include <vector>
#include "audio/AudioManager.h"
#include "audio/AIFCDecode.h"
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

    if(!metadata[1].contains("us")){
        return false;
    }

    LUS::BinaryWriter aifcWriter;
    AudioManager::Instance->create_aifc(metadata[1]["us"][1], aifcWriter);
    std::vector<char> aifcData = aifcWriter.ToVector();
    aifcWriter.Close();

    write_aiff(aifcData, *writer);

    return true;
}
