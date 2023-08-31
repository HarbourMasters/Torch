#include "AudioFactory.h"

#include <vector>
#include <iostream>
#include <filesystem>
#include <sstream>
#include "Companion.h"
#include "utils/MIODecoder.h"
#include "binarytools/BinaryReader.h"

namespace fs = std::filesystem;

#define NONE 0xFFFF

std::unordered_map<std::string, uint32_t> nameTable;

struct ALSeqData {
    uint8_t *offset;
    uint32_t len;
};

struct Entry {
    uint32_t offset;
    uint32_t length;
};

struct SampleBank {
	std::string name;
	uint32_t offset;
	std::vector<uint8_t> data;
};

struct TBLFile {
	std::vector<SampleBank> banks;
	std::vector<std::string> tbls;
	std::unordered_map<std::string, uint32_t> map;
};

struct CTLHeader {
    uint32_t instruments;
    uint32_t numDrums;
    uint32_t shared;
};

struct Sound {
    int32_t offset;
    float tuning;
};

struct Bank {
    std::string name;
    SampleBank sampleBank;
};

struct Inst {
    std::string name;
    uint32_t offset;
    uint8_t releaseRate;
    uint8_t normalRangeLo;
    uint8_t normalRangeHi;
    int32_t envelope;
    std::optional<Sound> soundLo;
    std::optional<Sound> soundMed;
    std::optional<Sound> soundHi;
};

struct Drum {
    std::string name;
    uint32_t offset;
    uint8_t releaseRate;
    uint8_t pan;
    int32_t envelope;
    Sound sound;
};

template<typename T>
std::vector<T> slice(std::vector<T> const &v, uint32_t m, uint32_t n) {
	auto first = v.cbegin() + m;
	auto last = v.cbegin() + n;

	std::vector<T> vec(first, last);
	return vec;
}

std::vector<uint32_t> range(uint32_t start, uint32_t end) {
    std::vector<uint32_t> result;
    for (uint32_t i = start; i < end; ++i) {
        result.push_back(i);
    }
    return result;
}

template <typename T1, typename T2, typename T3>
std::vector<std::tuple<size_t, T2, T3>> zip(const std::vector<T1>& container1, const std::vector<T2>& container2, const std::vector<T3>& container3) {
    std::vector<std::tuple<size_t, T2, T3>> result;

    size_t minSize = std::min({container1.size(), container2.size(), container3.size()});

    for (size_t i = 0; i < minSize; ++i) {
        result.emplace_back(container1[i], container2[i], container3[i]);
    }

    return result;
}

std::vector<Entry> parseSeqFile(std::vector<uint8_t>& buffer, int32_t offset){
    std::vector<Entry> entries;
    LUS::BinaryReader reader((char*) buffer.data(), buffer.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    uint16_t magic = reader.ReadUInt16();
    uint16_t num_entries = reader.ReadUInt16();

    for (int i = 0; i < num_entries; ++i) {
        reader.Seek((offset + 4) + (i * 8), LUS::SeekOffsetType::Start);
        entries.push_back({reader.ReadUInt32(), reader.ReadUInt32()});
    }

    reader.Close();
    return entries;
}

CTLHeader parseCTLHeader(std::vector<uint8_t>& data){
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    CTLHeader header = { reader.ReadUInt32(), reader.ReadUInt32(), reader.ReadUInt32() };

    reader.Close();
    return header;
}

std::string gen_name(const std::string& prefix){
    if(!nameTable.contains(prefix)){
        nameTable[prefix] = 0;
    }
    nameTable[prefix] += 1;
    return prefix + "-" + std::to_string(nameTable[prefix]);
}

std::optional<Sound> parseSound(std::vector<uint8_t> data) {
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    int32_t addr = reader.ReadInt32();
    float tuning = reader.ReadFloat();

    if(addr == 0){
        assert(tuning == 0.0f);
        return std::nullopt;
    }

    Sound sound = { addr, tuning };

    reader.Close();
    return sound;
}

Drum parseDrum(std::vector<uint8_t>& data, uint32_t addr) {
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);
    std::string name = gen_name("drum");

    uint8_t releaseRate = reader.ReadInt8();
    uint8_t pan = reader.ReadInt8();

    reader.Seek(12, LUS::SeekOffsetType::Start);
    Sound sound = parseSound(slice(data, 4, 12)).value();
    int32_t envOffset = reader.ReadInt32();
    assert(envOffset != 0);

    Drum drum = { name, addr, releaseRate, pan, envOffset, sound };

    return drum;
}

Inst parseInst(std::vector<uint8_t>& data, uint32_t addr) {
    std::string name = gen_name("inst");
    char* rawData = (char*) data.data();
    uint8_t normalRangeLo = data[1];
    uint8_t normalRangeHi = data[2];
    uint8_t releaseRate = data[3];

    int32_t envAddr;
    memcpy(&envAddr, rawData + 4, 4);
    envAddr = BSWAP32(envAddr);

    auto soundLo = parseSound(slice(data, 8, 16));
    auto soundMed = parseSound(slice(data, 16, 24));
    auto soundHi = parseSound(slice(data, 24, 32));

    if (soundLo == std::nullopt) {
        assert(normalRangeLo == 0);
    }
    if (soundHi == std::nullopt) {
        assert(normalRangeHi == 127);
    }

    Inst inst = { name, addr, releaseRate, normalRangeLo, normalRangeHi, envAddr, soundLo, soundMed, soundHi };
    return inst;
}

void parseCTL(CTLHeader header, std::vector<uint8_t>& data, SampleBank bank, uint32_t index) {
    nameTable.clear();
    std::ostringstream ss;
    ss << std::hex << std::setw(2) << std::setfill('0') << index;
    std::string name = ss.str();
    uint32_t numInstruments = header.instruments;
    uint32_t numDrums = header.numDrums;
    char* rawData = (char*) data.data();

    uint32_t drumBaseAddr;
    memcpy(&drumBaseAddr, rawData, 4);
    drumBaseAddr = BSWAP32(drumBaseAddr);

    std::vector<uint32_t> drumOffsets;

    if(numDrums != 0){
        for (size_t i = 0; i < numDrums; ++i) {
            uint32_t drumOffset;
            memcpy(&drumOffset, rawData + drumBaseAddr + (i * 4), 4);
            drumOffsets.push_back(BSWAP32(drumOffset));
        }
    }

    uint32_t instrumentBaseAddr = 4;
    std::vector<uint32_t> instrumentOffsets;
    std::vector<uint32_t> instrumentList;

    for (size_t i = 0; i < numInstruments; ++i) {
        uint32_t instOffset;
        memcpy(&instOffset, rawData + instrumentBaseAddr + (i * 4), 4);
        instOffset = BSWAP32(instOffset);
        if(instOffset == 0){
            instrumentList.push_back(NONE);
        } else {
            instrumentOffsets.push_back(instOffset);
            instrumentList.push_back(instOffset);
        }
    }

    std::sort(instrumentOffsets.begin(), instrumentOffsets.end());

    std::vector<Inst> insts;
    for(auto &offset : instrumentOffsets){
        auto rInst = slice(data, offset, offset + 32);
        Inst inst = parseInst(rInst, offset);
        insts.push_back(inst);
    }

    std::vector<Drum> drums;
    for(auto &offset : drumOffsets){
        auto rDrum = slice(data, offset, offset + 16);
        Drum drum = parseDrum(rDrum, offset);
        drums.push_back(drum);
    }
}

TBLFile parseTBL(std::vector<uint8_t>& data, std::vector<Entry>& entries) {
	TBLFile tbl;
	std::unordered_map<uint32_t, std::string> cache;
	for(auto &entry : entries){
		if(!cache.contains(entry.offset)){
			std::string name = gen_name("sample_bank");
			tbl.banks.push_back({name, entry.offset, slice(data, entry.offset, entry.offset + entry.length)});
            tbl.map[name] = tbl.banks.size() - 1;
			cache[entry.offset] = name;
		}
		tbl.tbls.push_back(cache[entry.offset]);
	}
	cache.clear();
	return tbl;
}

bool AudioFactory::process(LUS::BinaryWriter* writer, nlohmann::json& data, std::vector<uint8_t>& buffer) {
    auto metadata = data["offsets"];
	std::string path = data["path"];

	if(path.find("@sound") != std::string::npos){
		auto offsets = metadata["us"];
		std::vector<Entry> tbl = parseSeqFile(buffer, offsets["sound_tbl"][0]);
		std::vector<Entry> ctl = parseSeqFile(buffer, offsets["sound_ctl"][0]);

		std::cout << "Table entries: "   << tbl.size() << std::endl;
		std::cout << "Control entries: " << ctl.size() << std::endl;

		std::vector<uint8_t> tblData = slice(buffer, offsets["sound_tbl"][0], (size_t) offsets["sound_tbl"][0] + (size_t) offsets["sound_tbl"][1]);
        std::vector<uint8_t> ctlData = slice(buffer, offsets["sound_ctl"][0], (size_t) offsets["sound_ctl"][0] + (size_t) offsets["sound_ctl"][1]);
		TBLFile tblFile = parseTBL(tblData, tbl);

        std::cout << "TBLs: " << tblFile.tbls.size() << std::endl;
		std::cout << "Banks: " << tblFile.banks.size() << std::endl;
		std::cout << "Map: " << tblFile.map.size() << std::endl;

        auto zipped = zip(range(0, ctl.size()), ctl, tblFile.tbls);

        for (const auto& item : zipped) {
            auto [index, ctrl, sample_bank_name] = item;
            auto sample_bank = tblFile.map[sample_bank_name];
            auto entry = slice(ctlData, ctrl.offset, ctrl.offset + ctrl.length);
            auto headerRaw = slice(entry, 0, 16);
            auto header = parseCTLHeader(headerRaw);
            std::cout << "Instruments: " << header.instruments << std::endl;
            std::cout << "Drums: " << header.numDrums << std::endl;
        }

		exit(0);
	}

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
	}

	if(path.find("sound_tbl") != std::string::npos){
		// WRITE_HEADER(LUS::ResourceType::Blob, 0);
		// std::vector<Entry> entries = parseSeqFile(buffer, metadata[1]["us"][0]);
		// for(auto & entry : entries){
		// 	   std::cout << "Offset: " << entrie.offset << std::endl;
		// 	   std::cout << "Len: " << entrie.length << std::endl;
		// }
	}

	return false;
}
