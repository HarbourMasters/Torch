#include "AudioManager.h"

#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "hj/pyutils.h"
#include "binarytools/BinaryReader.h"
#include "hj/zip.h"
#include <nlohmann/json.hpp>

std::unordered_map<std::string, uint32_t> name_table;
AudioManager* AudioManager::Instance;

std::string gen_name(const std::string& prefix){
    if(!name_table.contains(prefix)){
        name_table[prefix] = 0;
    }
    name_table[prefix] += 1;
    return prefix + "-" + std::to_string(name_table[prefix]);
}

AifcEntry SampleBank::AddSample(uint32_t addr, size_t sampleSize, const Book& book, const Loop& loop){
    assert(sampleSize % 2 == 0);
    if(sampleSize % 9 != 0){
        assert(sampleSize % 9 == 1);
        sampleSize -= 1;
    }

    AifcEntry entry;

    if(this->entries.contains(addr)){
        entry = this->entries[addr];
        assert(entry.book == book);
        assert(entry.loop == loop);
        assert(entry.data.size() == sampleSize);
    } else {
        entry = { gen_name("aifc"), PyUtils::slice(this->data, addr, addr + sampleSize), book, loop };
        this->entries[addr] = entry;
    }

    return entry;
}

void Bank::print() const {
    std::cout << "Bank: " << name << std::endl;
    std::cout << "Instruments: " << std::to_string(insts.size()) << std::endl;
    std::cout << "Drums: " << std::to_string(drums.size()) << std::endl;
    std::cout << "Samples: " << std::to_string(samples.size()) << std::endl;
    std::cout << "Envelopes: " << std::to_string(envelopes.size()) << std::endl;
    std::cout << "All Instruments: " << std::to_string(allInsts.size()) << std::endl;
    std::cout << "Inst Offsets: " << std::to_string(instOffsets.size()) << std::endl;
    std::cout << "Sample Bank: " << sampleBank.name << std::endl;
    std::cout << "Sample Bank Offset: " << std::to_string(sampleBank.offset) << std::endl;
}

std::vector<Entry> AudioManager::parse_seq_file(std::vector<uint8_t>& buffer, uint32_t offset, bool isCTL){
    std::vector<Entry> entries;
    LUS::BinaryReader reader((char*) buffer.data(), buffer.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    unsigned short magic = reader.ReadUInt16();
    unsigned short num_entries = reader.ReadUInt16();
    uint32_t prev = ALIGN(4 + num_entries * 8, 16);

    assert(magic == (isCTL ? 1 : 2));

    for (int i = 0; i < num_entries; ++i) {
        reader.Seek((offset + 4) + (i * 8), LUS::SeekOffsetType::Start);
        uint32_t addr = reader.ReadUInt32();
        uint32_t length = reader.ReadUInt32();

        if(isCTL){
            assert(addr == prev);
        } else {
            assert(addr <= prev);
        }

        prev = std::max(prev, addr + length);
        entries.push_back({addr, length});
    }

    reader.Close();
    return entries;
}

CTLHeader AudioManager::parse_ctl_header(std::vector<uint8_t>& data){
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    CTLHeader header = { reader.ReadUInt32(), reader.ReadUInt32(), reader.ReadUInt32() };

    reader.Close();
    return header;
}

std::optional<Sound> AudioManager::parse_sound(std::vector<uint8_t> data) {
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    uint32_t addr = reader.ReadUInt32();
    float tuning = reader.ReadFloat();

    if(addr == 0){
        assert(tuning == 0.0f);
        return std::nullopt;
    }

    Sound sound = { addr, tuning };

    reader.Close();
    return sound;
}

Drum AudioManager::parse_drum(std::vector<uint8_t>& data, uint32_t addr) {
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);
    std::string name = gen_name("drum");

    uint8_t releaseRate = reader.ReadInt8();
    uint8_t pan = reader.ReadInt8();

    reader.Seek(12, LUS::SeekOffsetType::Start);
    Sound sound = parse_sound(PyUtils::slice(data, 4, 12)).value();
    uint32_t envOffset = reader.ReadInt32();
    assert(envOffset != 0);

    Drum drum = { name, addr, releaseRate, pan, envOffset, sound };

    return drum;
}

Inst AudioManager::parse_inst(std::vector<uint8_t>& data, uint32_t addr) {
    std::string name = gen_name("inst");
    uint8_t normalRangeLo = data[1];
    uint8_t normalRangeHi = data[2];
    uint8_t releaseRate = data[3];

    uint32_t envAddr;
    memcpy(&envAddr, (char*) data.data() + 4, 4);
    envAddr = BSWAP32(envAddr);

    assert(envAddr != 0);

    auto soundLo  = parse_sound(PyUtils::slice(data, 8, 16));
    auto soundMed = parse_sound(PyUtils::slice(data, 16, 24));
    auto soundHi  = parse_sound(PyUtils::slice(data, 24));

    if (soundLo == std::nullopt) {
        assert(normalRangeLo == 0);
    }
    if (soundHi == std::nullopt) {
        assert(normalRangeHi == 127);
    }

    Inst inst = { name, addr, releaseRate, normalRangeLo, normalRangeHi, envAddr, soundLo, soundMed, soundHi };
    return inst;
}

Loop AudioManager::parse_loop(uint32_t addr, std::vector<uint8_t>& bankData){
    LUS::BinaryReader reader((char*) bankData.data(), bankData.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(addr, LUS::SeekOffsetType::Start);

    std::optional<std::vector<short>> state = std::nullopt;
    uint32_t start = reader.ReadUInt32();
    uint32_t end = reader.ReadUInt32();
    int32_t count = reader.ReadInt32();
    uint32_t pad = reader.ReadUInt32();
    if(count != 0){
        state = std::vector<short>();
        for (size_t i = 0; i < 16; ++i) {
            state.value().push_back(reader.ReadShort());
        }
    }
    Loop loop = { start, end, count, pad, state };
    return loop;
}

Book AudioManager::parse_book(uint32_t addr, std::vector<uint8_t>& bankData){
    LUS::BinaryReader reader((char*) bankData.data(), bankData.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(addr, LUS::SeekOffsetType::Start);

    uint32_t order = reader.ReadInt32();
    uint32_t npredictors = reader.ReadInt32();
    std::vector<short> table;
    std::vector<uint8_t> tableData = PyUtils::slice(bankData, addr + 8, addr + 8 + 16 * order * npredictors);
    for (size_t i = 0; i < ( 16 * order * npredictors ); i += 2) {
        short dtable;
        memcpy(&dtable, tableData.data() + i, 2);
        table.push_back(BSWAP16(dtable));
    }

    Book book = { order, npredictors, table };
    return book;
}

AifcEntry AudioManager::parse_sample(std::vector<uint8_t>& data, std::vector<uint8_t>& bankData, SampleBank& sampleBank){
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    uint32_t zero = reader.ReadUInt32();
    uint32_t addr = reader.ReadUInt32();
    uint32_t loop = reader.ReadUInt32();
    uint32_t book = reader.ReadUInt32();
    uint32_t sampleSize = reader.ReadUInt32();
    assert(zero == 0);
    assert(loop != 0);
    assert(book != 0);

    Loop loopData = parse_loop(loop, bankData);
    Book bookData = parse_book(book, bankData);

    reader.Close();
    return sampleBank.AddSample(addr, sampleSize, bookData, loopData);
}


std::vector<EnvelopeData> AudioManager::parse_envelope(uint32_t addr, std::vector<uint8_t>& dataBank){
    std::vector<EnvelopeData> entries;
    LUS::BinaryReader reader((char*) dataBank.data(), dataBank.size());
    reader.SetEndianness(LUS::Endianness::Big);

    while(true){
        reader.Seek(addr, LUS::SeekOffsetType::Start);
        unsigned short delay = reader.ReadUShort();
        unsigned short arg = reader.ReadUShort();
        entries.emplace_back(delay, arg);
        addr += 4;
        if (1 <= (-delay) % ((uint32_t) pow(2, 16)) <= 3.0){
            break;
        }
    }

    reader.Close();
    return entries;
}

Bank AudioManager::parse_ctl(CTLHeader header, std::vector<uint8_t> data, SampleBank bank, uint32_t index) {
    name_table.clear();
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
        memcpy(&instOffset, rawData + (instrumentBaseAddr + (i * 4)), 4);
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
        auto rInst = PyUtils::slice(data, offset, offset + 32);
        Inst inst = parse_inst(rInst, offset);
        insts.push_back(inst);
    }

    std::vector<Drum> drums;
    for(auto &offset : drumOffsets){
        auto rDrum = PyUtils::slice(data, offset, offset + 16);
        Drum drum = parse_drum(rDrum, offset);
        drums.push_back(drum);
    }

    auto envOffsets = std::vector<uint32_t>();
    auto sampleOffsets = std::vector<uint32_t>();
    auto tunings = std::unordered_map<uint32_t, float>();

    for(auto &inst : insts){
        for(auto &sound : {inst.soundLo, inst.soundMed, inst.soundHi}){
            if(sound.has_value()){
                sampleOffsets.push_back(sound.value().offset);
                tunings[sound.value().offset] = sound.value().tuning;
            }
        }
        envOffsets.push_back(inst.envelope);
    }
    for(auto &drum : drums){
        sampleOffsets.push_back(drum.sound.offset);
        tunings[drum.sound.offset] = drum.sound.tuning;
        envOffsets.push_back(drum.envelope);
    }
    // Put drums somewhere in the middle of the instruments to make sample
    // addresses come in increasing order. (This logic isn't totally right,
    // but it works for our purposes.)
    std::vector<std::variant<Inst, std::vector<Drum>>> allInsts;
    bool needDrums = !drums.empty();
    for(auto &inst : insts){
        std::vector<std::optional<Sound>> sounds = {inst.soundLo, inst.soundMed, inst.soundHi};

        if(needDrums && std::any_of(sounds.cbegin(), sounds.cend(), [&drums](std::optional<Sound> sound){ return sound.has_value() && sound.value().offset > drums[0].sound.offset; })){
            allInsts.emplace_back(drums);
            needDrums = false;
        }

        allInsts.emplace_back(inst);
    }


    if(needDrums){
        allInsts.emplace_back(drums);
    }

    std::unordered_map<uint32_t, AifcEntry> samples;
    std::sort(sampleOffsets.begin(), sampleOffsets.end());
    for(auto &offset : sampleOffsets){
        auto rSample = PyUtils::slice(data, offset, offset + 20);
        AifcEntry sample = parse_sample(rSample, data, bank);
        for(auto &tuning : tunings){
            sample.tunings.push_back(tuning.second);
        }
        samples[offset] = sample;
    }

    std::unordered_map<uint32_t, std::vector<EnvelopeData>> envData;
    std::vector<uint32_t> usedEnvOffsets;
    std::sort(envOffsets.begin(), envOffsets.end());
    for(auto &offset : envOffsets){
        auto env = parse_envelope(offset, data);
        envData[offset] = env;
        for(int i = 0; i < ALIGN(env.size(), 4); i++){
            usedEnvOffsets.push_back(offset + (i * 4));
        }
    }

    std::vector<uint32_t> unusedEnvOffsets;
    if(!usedEnvOffsets.empty()){
        size_t min = std::min_element(usedEnvOffsets.begin(), usedEnvOffsets.end()) - usedEnvOffsets.begin();
        size_t max = std::max_element(usedEnvOffsets.begin(), usedEnvOffsets.end()) - usedEnvOffsets.begin();
        for(size_t idx = min + 4; idx < max; idx += 4){
            uint32_t addr = usedEnvOffsets[idx];
            if(std::find(usedEnvOffsets.begin(), usedEnvOffsets.end(), addr) == usedEnvOffsets.end()){
                unusedEnvOffsets.push_back(addr);
                uint32_t stubMarker;
                memcpy(&stubMarker, rawData + addr, 4);
                stubMarker = BSWAP32(stubMarker);
                assert(stubMarker == 0);
                auto env = parse_envelope(addr, data);
                envData[addr] = env;
                for(int i = 0; i < ALIGN(env.size(), 4); i++){
                    usedEnvOffsets.push_back(addr + (i * 4));
                }
            }
        }
    }

    std::unordered_map<uint32_t, Envelope> envelopes;
    for(auto &entry : envData){
        Envelope env = { gen_name("envelope"), entry.second };
        envelopes[entry.first] = env;
    }

    Bank bankData = { name, bank, insts, drums, allInsts, instrumentList, envelopes, samples };
    return bankData;
}

TBLFile AudioManager::parse_tbl(std::vector<uint8_t>& data, std::vector<Entry>& entries) {
    TBLFile tbl;
    std::unordered_map<uint32_t, std::string> cache;
    for(auto &entry : entries){
        if(!cache.contains(entry.offset)){
            std::string name = gen_name("sample_bank");
            SampleBank sampleBank = {
                    name, entry.offset, PyUtils::slice(data, entry.offset, entry.offset + entry.length)
            };
            tbl.banks.push_back(sampleBank);
            tbl.map[name] = tbl.banks.size() - 1;
            cache[entry.offset] = name;
        }
        tbl.tbls.push_back(cache[entry.offset]);
    }
    cache.clear();
    return tbl;
}

void AudioManager::initialize(std::vector<uint8_t>& buffer) {
    auto assets = nlohmann::json::parse(std::ifstream( "assets.json" ));
    auto sound_data = assets["@sound_data"]["us"];
    auto sound_tbl = sound_data["sound_tbl"];
    auto sound_ctl = sound_data["sound_ctl"];

    std::vector<Entry> tbl = parse_seq_file(buffer, sound_tbl[0], false);
    std::vector<Entry> ctl = parse_seq_file(buffer, sound_ctl[0], true);

    std::cout << "Table entries: "   << tbl.size() << std::endl;
    std::cout << "Control entries: " << ctl.size() << std::endl;

    std::vector<uint8_t> tbl_data = PyUtils::slice(buffer, sound_tbl[0], (size_t) sound_tbl[0] + (size_t) sound_tbl[1]);
    std::vector<uint8_t> ctl_data = PyUtils::slice(buffer, sound_ctl[0], (size_t) sound_ctl[0] + (size_t) sound_ctl[1]);
    TBLFile tbl_file = parse_tbl(tbl_data, tbl);

    std::cout << "TBLs: " << tbl_file.tbls.size() << std::endl;
    std::cout << "Banks: " << tbl_file.banks.size() << std::endl;
    std::cout << "Map: " << tbl_file.map.size() << std::endl;

    auto zipped = zip(PyUtils::range(0, ctl.size()), ctl, tbl_file.tbls);

    std::cout << "================================" << std::endl;
    for (const auto& item : zipped) {
        auto [index, ctrl, sample_bank_name] = item;
        auto sample_bank = tbl_file.map[sample_bank_name];
        auto entry = PyUtils::slice(ctl_data, ctrl.offset, ctrl.offset + ctrl.length);
        auto headerRaw = PyUtils::slice(entry, 0, 16);
        auto header = parse_ctl_header(headerRaw);
        auto bank = parse_ctl(header, PyUtils::slice(entry, 16), tbl_file.banks[sample_bank], index);
        banks.push_back(bank);
        bank.print();
        std::cout << "================================" << std::endl;
    }
}
