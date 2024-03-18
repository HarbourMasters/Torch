#include "AudioManager.h"

#include <vector>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cassert>
#include <cstring>
#include <factories/BaseFactory.h>

#include "hj/zip.h"
#include "hj/pyutils.h"
#include "spdlog/spdlog.h"
#include "binarytools/BinaryReader.h"
#include "spdlog/spdlog.h"

std::unordered_map<std::string, uint32_t> name_table;
std::unordered_map<uint32_t, std::string> sample_table;
AudioManager* AudioManager::Instance;

std::vector<uint32_t> PyUtils::range(uint32_t start, uint32_t end) {
    std::vector<uint32_t> result;
    for (uint32_t i = start; i < end; ++i) {
        result.push_back(i);
    }
    return result;
}

std::string gen_name(const std::string& prefix){
    if(!name_table.contains(prefix)){
        name_table[prefix] = 0;
    }
    return prefix + std::to_string(name_table[prefix]++);
}

AudioBankSample* SampleBank::AddSample(uint32_t addr, size_t sampleSize, const AdpcmBook& book, const AdpcmLoop& loop){
    assert(sampleSize % 2 == 0);
    if(sampleSize % 9 != 0){
        assert(sampleSize % 9 == 1);
        sampleSize -= 1;
    }

    AudioBankSample* entry;

    if(this->entries.contains(addr)){
        entry = this->entries[addr];
        assert(entry->book == book);
        assert(entry->loop == loop);
        assert(entry->data.size() == sampleSize);
    } else {
        entry = new AudioBankSample{ gen_name("aifc"), PyUtils::slice(this->data, addr, addr + sampleSize), book, loop };
        this->entries[addr] = entry;
    }

    return entry;
}

void Bank::print() const {
    SPDLOG_DEBUG("Bank: {}", name);
    SPDLOG_DEBUG("Instruments: {}", std::to_string(insts.size()));
    SPDLOG_DEBUG("Drums: {}", std::to_string(drums.size()));
    SPDLOG_DEBUG("Samples: {}", std::to_string(samples.size()));
    SPDLOG_DEBUG("Envelopes: {}", std::to_string(envelopes.size()));
    SPDLOG_DEBUG("All Instruments: {}", std::to_string(allInsts.size()));
    SPDLOG_DEBUG("Inst Offsets: {}", std::to_string(instOffsets.size()));
    SPDLOG_DEBUG("Sample Bank: {}", sampleBank->name);
    SPDLOG_DEBUG("Sample Bank Offset: {}", std::to_string(sampleBank->offset));
}

std::vector<Entry> AudioManager::parse_seq_file(std::vector<uint8_t>& buffer, uint32_t offset, bool isCTL){
    std::vector<Entry> entries;
    LUS::BinaryReader reader((char*) buffer.data(), buffer.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    uint16_t magic = reader.ReadUInt16();
    uint16_t num_entries = reader.ReadUInt16();
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

uint32_t parse_bcd(std::vector<uint8_t>& data){
    uint32_t result = 0;
    for(auto &c : data){
        result *= 10;
        result += c >> 4;
        result *= 10;
        result += c & 15;
    }

    return result;
}

CTLHeader AudioManager::parse_ctl_header(std::vector<uint8_t>& data){
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    auto numInstruments = reader.ReadUInt32();
    auto numDrums = reader.ReadUInt32();
    auto shared = reader.ReadUInt32();
    auto raw = PyUtils::slice(data, 12, 16);
    auto date = parse_bcd(raw);
    uint16_t year = date / 10000;
    uint16_t month = (date / 100) % 100;
    uint16_t day = date % 100;
    std::ostringstream iso;
    iso << year << "-" << std::setw(2) << std::setfill('0') << month << "-" << std::setw(2) << std::setfill('0') << day;
    assert(shared == 0 || shared == 1);

    CTLHeader header = { reader.ReadUInt32(), reader.ReadUInt32(), iso.str() };

    reader.Close();
    return header;
}

Bank AudioManager::parse_ctl(CTLHeader header, std::vector<uint8_t> data, SampleBank* bank, uint32_t index, bool hasHeaders) {
    name_table.clear();
    std::ostringstream ss;
    ss << std::hex << std::setw(2) << std::setfill('0') << index;
    std::string name = ss.str();
    uint32_t numInstruments = header.instruments;
    uint32_t numDrums = header.numDrums;
    LUS::BinaryReader reader(reinterpret_cast<char*>(data.data()), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    uint32_t drumBaseAddr = reader.ReadUInt32();

    std::vector<uint32_t> drumOffsets;

    if(numDrums != 0){
        assert(drumBaseAddr != 0);
        for (size_t i = 0; i < numDrums; ++i) {
            reader.Seek(drumBaseAddr + i * 4, LUS::SeekOffsetType::Start);
            uint32_t drumOffset = reader.ReadUInt32();
            // assert(drumOffset != 0);
            drumOffsets.push_back(drumOffset);
        }
    } else {
        assert(drumBaseAddr == 0);
    }

    const uint32_t instrumentBaseAddr = 4;
    std::vector<uint32_t> instrumentOffsets;
    std::vector<uint32_t> instrumentList;

    for (size_t i = 0; i < numInstruments; ++i) {
        reader.Seek(instrumentBaseAddr + i * 4, LUS::SeekOffsetType::Start);
        uint32_t instOffset = reader.ReadUInt32();
        if(instOffset == 0){
            instrumentList.push_back(NONE);
            instrumentOffsets.push_back(NONE);
        } else {
            instrumentOffsets.push_back(instOffset);
            instrumentList.push_back(instOffset);
        }
    }

    std::vector<Instrument> insts;
    for(auto &offset : instrumentOffsets){
        if(offset == NONE){
            Instrument invalid = { .valid = false };
            insts.push_back(invalid);
            continue;
        }
        auto rInst = PyUtils::slice(data, offset, offset + 32);
        Instrument inst = parse_inst(rInst, offset);
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
    std::vector<std::variant<Instrument, std::vector<Drum>>> allInsts;
    bool needDrums = !drums.empty();
    for(auto &inst : insts){
        std::vector<std::optional<AudioBankSound>> sounds = {inst.soundLo, inst.soundMed, inst.soundHi};

        if(needDrums && std::any_of(sounds.cbegin(), sounds.cend(), [&drums](std::optional<AudioBankSound> sound){ return sound.has_value() && sound.value().offset > drums[0].sound.offset; })){
            allInsts.emplace_back(drums);
            needDrums = false;
        }

        allInsts.emplace_back(inst);
    }

    if(needDrums){
        allInsts.emplace_back(drums);
    }

    std::map<uint32_t, AudioBankSample*> samples;
    std::sort(sampleOffsets.begin(), sampleOffsets.end());
    for(auto &offset : sampleOffsets){
        auto rSample = PyUtils::slice(data, offset, offset + (hasHeaders ? 16 : 20));
        AudioBankSample* sample = parse_sample(rSample, data, bank, hasHeaders);
        for(auto &tuning : tunings){
            sample->tunings.push_back(tuning.second);
        }
        samples[offset] = sample;
    }

    std::unordered_map<uint32_t, std::vector<AdsrEnvelope>> envData;
    std::vector<uint32_t> usedEnvOffsets;
    std::sort(envOffsets.begin(), envOffsets.end());
    for(auto &offset : envOffsets){
        auto env = parse_envelope(offset, data);
        envData[offset] = env;
        for(int i = 0; i < ALIGN(env.size(), 4); i++){
            usedEnvOffsets.push_back(offset + (i * 4));
        }
    }

    if(!usedEnvOffsets.empty()){
        std::vector<uint32_t> unusedEnvOffsets;
        size_t min = std::min_element(usedEnvOffsets.begin(), usedEnvOffsets.end()) - usedEnvOffsets.begin();
        size_t max = std::max_element(usedEnvOffsets.begin(), usedEnvOffsets.end()) - usedEnvOffsets.begin();
        for(size_t idx = min + 4; idx < max; idx += 4){
            uint32_t addr = usedEnvOffsets[idx];
            if(std::find(usedEnvOffsets.begin(), usedEnvOffsets.end(), addr) == usedEnvOffsets.end()){
                unusedEnvOffsets.push_back(addr);
                reader.Seek(addr, LUS::SeekOffsetType::Start);
                uint32_t stubMarker = reader.ReadUInt32();
                assert(stubMarker == 0);
                auto env = parse_envelope(addr, data);
                envData[addr] = env;
                for(int i = 0; i < ALIGN(env.size(), 4); i++){
                    usedEnvOffsets.push_back(addr + (i * 4));
                }
            }
        }
    }

    std::map<uint32_t, Envelope> envelopes;
    for(auto &entry : envData){
        Envelope env = { gen_name("envelope"), entry.second };
        envelopes[entry.first] = env;
    }

    Bank bankData = { name, bank, insts, drums, allInsts, instrumentList, envelopes, samples };
    return bankData;
}

std::optional<AudioBankSound> AudioManager::parse_sound(std::vector<uint8_t> data) {
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    uint32_t addr = reader.ReadUInt32();
    float tuning = reader.ReadFloat();

    if(addr == 0){
        assert(tuning == 0.0f);
        return std::nullopt;
    }

    AudioBankSound sound = { addr, tuning };

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
    AudioBankSound sound = parse_sound(PyUtils::slice(data, 4, 12)).value();
    uint32_t envOffset = reader.ReadInt32();
    assert(envOffset != 0);

    Drum drum = { name, addr, releaseRate, pan, envOffset, sound };

    return drum;
}

Instrument AudioManager::parse_inst(std::vector<uint8_t>& data, uint32_t addr) {
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

    Instrument inst = { true, name, addr, releaseRate, normalRangeLo, normalRangeHi, envAddr, soundLo, soundMed, soundHi };
    return inst;
}

AdpcmLoop AudioManager::parse_loop(uint32_t addr, std::vector<uint8_t>& bankData){
    LUS::BinaryReader reader((char*) bankData.data(), bankData.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(addr, LUS::SeekOffsetType::Start);

    std::optional<std::vector<int16_t>> state = std::nullopt;
    uint32_t start = reader.ReadUInt32();
    uint32_t end = reader.ReadUInt32();
    int32_t count = reader.ReadInt32();
    uint32_t pad = reader.ReadUInt32();

    if(count != 0){
        state = std::vector<int16_t>();
        for (size_t i = 0; i < 16; ++i) {
            state.value().push_back(reader.ReadInt16());
        }
    }
    AdpcmLoop loop = { start, end, count, pad, state };
    return loop;
}

AdpcmBook AudioManager::parse_book(uint32_t addr, std::vector<uint8_t>& bankData){
    LUS::BinaryReader reader((char*) bankData.data(), bankData.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(addr, LUS::SeekOffsetType::Start);

    int32_t order = reader.ReadInt32();
    int32_t npredictors = reader.ReadInt32();

    // assert(order == 2);
    // assert(npredictors == 2);

    std::vector<int16_t> table;
    std::vector<uint8_t> tableData = PyUtils::slice(bankData, addr + 8, addr + 8 + 16 * order * npredictors);
    for (size_t i = 0; i < ( 16 * order * npredictors ); i += 2) {
        int16_t dtable;
        memcpy(&dtable, tableData.data() + i, 2);
        table.push_back(BSWAP16(dtable));
    }

    AdpcmBook book = { order, npredictors, table };
    return book;
}

AudioBankSample* AudioManager::parse_sample(std::vector<uint8_t>& data, std::vector<uint8_t>& bankData, SampleBank* sampleBank, bool hasHeaders){
    LUS::BinaryReader reader((char*) data.data(), data.size());
    reader.SetEndianness(LUS::Endianness::Big);

    uint32_t addr;
    uint32_t loop;
    uint32_t book;
    uint32_t sampleSize;

    if(hasHeaders){
        sampleSize = reader.ReadUInt32();
        addr = reader.ReadUInt32();
        loop = reader.ReadUInt32();
        book = reader.ReadUInt32();
    } else {
        uint32_t zero = reader.ReadUInt32();
        addr = reader.ReadUInt32();
        loop = reader.ReadUInt32();
        book = reader.ReadUInt32();
        sampleSize = reader.ReadUInt32();
        assert(zero == 0);
    }

    assert(loop != 0);
    assert(book != 0);

    AdpcmLoop loopData = parse_loop(loop, bankData);
    AdpcmBook bookData = parse_book(book, bankData);

    reader.Close();
    return sampleBank->AddSample(addr, sampleSize, bookData, loopData);
}


std::vector<AdsrEnvelope> AudioManager::parse_envelope(uint32_t addr, std::vector<uint8_t>& dataBank){
    std::vector<AdsrEnvelope> entries;
    LUS::BinaryReader reader((char*) dataBank.data(), dataBank.size());
    reader.SetEndianness(LUS::Endianness::Big);

    while(true){
        reader.Seek(addr, LUS::SeekOffsetType::Start);
        int16_t delay = reader.ReadInt16();
        int16_t arg = reader.ReadInt16();
        AdsrEnvelope entry = { delay, arg };
        entries.push_back(entry);
        addr += 4;
        if (1 <= (-delay) % (1 << 16) && (-delay) % (1 << 16) <= 3){
            break;
        }
    }

    reader.Close();
    return entries;
}

std::vector<Entry> AudioManager::parse_sh_header(std::vector<uint8_t>& data, bool isCTL) {
    LUS::BinaryReader reader(reinterpret_cast<char*>(data.data()), data.size());
    reader.SetEndianness(LUS::Endianness::Big);
    int16_t num_entries = reader.ReadInt16();
    uint32_t prev = 0;
    std::vector<Entry> entries;

    std::vector<uint8_t> expected(14, '\0');
    assert(std::equal(data.begin() + 2, data.begin() + 16, expected.begin()));

    for (size_t i = 0; i < num_entries; ++i) {
        reader.Seek(16 + 16 * i, LUS::SeekOffsetType::Start);
        uint32_t offset = reader.ReadUInt32();
        uint32_t length = reader.ReadUInt32();
        uint8_t medium = reader.ReadUByte();
        uint8_t cachePolicy = reader.ReadUByte();

        assert(length != 0);
        assert(offset == prev);
        prev = offset + length;

        // This validations are fine but for sm64 only
        // assert(medium == 0x02);
        // assert(cachePolicy == (!isCTL ? 0x04 : 0x03));
        SPDLOG_INFO("Medium: {}", medium);
        SPDLOG_INFO("Cache Policy: {}", cachePolicy);

        if(isCTL){
            auto sampleBankIndex = reader.ReadUByte();
            auto sampleBankIndex2 = reader.ReadUByte();
            auto numInstruments = reader.ReadUByte();
            auto numDrums = reader.ReadUByte();

            SPDLOG_INFO("Sample Bank Index: {}", sampleBankIndex);
            SPDLOG_INFO("Num Instruments: {}", numInstruments);
            SPDLOG_INFO("Num Drums: {}", numDrums);

            assert(reader.ReadUInt16() == 0);
            assert(sampleBankIndex2 == 0xFF);

            entries.push_back(Entry { offset, length, SHHeader {
                sampleBankIndex, -1, numInstruments, numDrums
            }});
        } else {
            assert(reader.ReadUInt32() + reader.ReadUInt16() == 0);
            entries.push_back(Entry { offset, length });
        }
    }

    reader.Close();
    return entries;
}

TBLFile AudioManager::parse_tbl(std::vector<uint8_t>& data, std::vector<Entry>& entries) {
    TBLFile tbl;
    std::unordered_map<uint32_t, std::string> cache;
    for(auto &entry : entries){
        if(!cache.contains(entry.offset)){
            std::string name = gen_name("sample_bank");
            auto* sampleBank = new SampleBank{
                name, entry.offset, PyUtils::slice(data, entry.offset, entry.offset + entry.length)
            };
            tbl.banks.push_back(sampleBank);
            tbl.map[name] = sampleBank;
            cache[entry.offset] = name;
        }
        tbl.tbls.push_back(cache[entry.offset]);
    }
    cache.clear();
    return tbl;
}

void AudioManager::initialize(std::vector<uint8_t>& buffer, YAML::Node& data) {

    auto ctl = GetSafeNode<YAML::Node>(data, "ctl");
    auto tbl = GetSafeNode<YAML::Node>(data, "tbl");
    auto ctlOffset = ctl["offset"].as<uint32_t>();
    auto ctlSize = ctl["size"].as<size_t>();

    auto tblOffset = tbl["offset"].as<uint32_t>();
    auto tblSize = tbl["size"].as<size_t>();

    auto tbl_data = PyUtils::slice(buffer, tblOffset, tblOffset + tblSize);
    auto ctl_data = PyUtils::slice(buffer, ctlOffset, ctlOffset + ctlSize);

    if(data["ctl_header"] && data["tbl_header"]){
        auto ctl_header = GetSafeNode<YAML::Node>(data, "ctl_header");
        auto tbl_header = GetSafeNode<YAML::Node>(data, "tbl_header");

        auto ctlHeaderOffset = GetSafeNode<uint32_t>(ctl_header, "offset");
        auto ctlHeaderSize = GetSafeNode<size_t>(ctl_header, "size");

        auto tblHeaderOffset = GetSafeNode<uint32_t>(tbl_header, "offset");
        auto tblHeaderSize = GetSafeNode<size_t>(tbl_header, "size");

        auto ctl_header_data = PyUtils::slice(buffer, ctlHeaderOffset, ctlHeaderOffset + ctlHeaderSize);
        auto tbl_header_data = PyUtils::slice(buffer, tblHeaderOffset, tblHeaderOffset + tblHeaderSize);

        auto ctlEntries = parse_sh_header(ctl_header_data, true);
        auto tblEntries = parse_sh_header(tbl_header_data, false);

        SPDLOG_INFO("Raw TBL Entries: {}", tblEntries.size());
        SPDLOG_INFO("Raw CTL Entries: {}", ctlEntries.size());

        this->loaded_tbl = parse_tbl(tbl_data, tblEntries);

        // This is a hack for sf64 because for the last ones it crashes
        // We need to fix this ASAP
        for(size_t idx = 0; idx < 23; idx++){
            auto ctrl = ctlEntries[idx];
            auto header = ctrl.header.value();
            auto entry = PyUtils::slice(ctl_data, ctrl.offset, ctrl.offset + ctrl.length);
            auto sample_bank = this->loaded_tbl.banks[header.sampleBankIndex];

            CTLHeader ch = {
                header.numInsts,
                header.numDrums,
                "0000-00-00"
            };

            if(idx == 24) {
                continue;
            }

            auto bank = parse_ctl(ch, entry, sample_bank, idx, true);
            banks[idx] = bank;
        }

    } else {
        auto tblEntries = parse_seq_file(buffer, tblOffset, false);
        auto ctlEntries = parse_seq_file(buffer, ctlOffset, true);

        SPDLOG_INFO("Raw TBL Entries: {}", tbl.size());
        SPDLOG_INFO("Raw CTL Entries: {}", ctl.size());

        this->loaded_tbl = parse_tbl(tbl_data, tblEntries);

        SPDLOG_INFO("Processed TBL Entries: {}", this->loaded_tbl.tbls.size());
        SPDLOG_INFO("Processed TBL Banks: {}", this->loaded_tbl.banks.size());

        auto zipped = zip(PyUtils::range(0, ctl.size()), ctlEntries, this->loaded_tbl.tbls);

        for (const auto& item : zipped) {
            auto [index, ctrl, sample_bank_name] = item;
            auto sample_bank = this->loaded_tbl.map[sample_bank_name];
            auto entry = PyUtils::slice(ctl_data, ctrl.offset, ctrl.offset + ctrl.length);
            auto headerRaw = PyUtils::slice(entry, 0, 16);
            auto header = parse_ctl_header(headerRaw);
            auto bank = parse_ctl(header, PyUtils::slice(entry, 16), sample_bank, index, false);
            banks[index] = bank;
            SPDLOG_INFO("Processed Bank {}", index);
        }
    }

    int32_t idx = -1;
    for(auto &sample_bank : this->loaded_tbl.banks){
        auto offsets = PyUtils::keys(sample_bank->entries);
        std::sort(offsets.begin(), offsets.end());

        for(auto &offset : offsets){
            this->sampleMap[sample_bank->entries[offset]] = ++idx;
        }
    }
}

void serialize_f80(double num, LUS::BinaryWriter &writer) {
    // Convert the input double to an uint64_t
    std::uint64_t f64;
    std::memcpy(&f64, &num, sizeof(double));

    std::uint64_t f64_sign_bit = f64 & (std::uint64_t) pow(2, 63);
    if (num == 0.0) {
        if (f64_sign_bit) {
            writer.Write(0x80000000);
        } else {
            writer.Write(0x00000000);
        }
    }

    std::uint64_t exponent = ((f64 ^ f64_sign_bit) >> 52);

    assert(exponent != 0);
    assert(exponent != 0x7FF);

    exponent -= 1023;
    uint64_t f64_mantissa_bits = f64 & (uint64_t) pow(2, 52) - 1;
    uint64_t f80_sign_bit = f64_sign_bit << (80 - 64);
    uint64_t f80_exponent = (exponent + 0x3FFF) << 64;
    uint64_t f80_mantissa_bits = (uint64_t) pow(2, 63) | (f64_mantissa_bits << (63 - 52));
    uint64_t f80 = f80_sign_bit | f80_exponent | f80_mantissa_bits;

    // Split the f80 representation into two parts (high and low)
    uint16_t high = BSWAP16((uint16_t) f80 >> 64);
    writer.Write((char*) &high, 2);
    uint64_t low = BSWAP64(f80 & ((uint64_t) pow(2, 64) - 1));
    writer.Write((char*) &low, 8);
}

#define START_SECTION(section) \
    { \
    out.Write((uint32_t) BSWAP32(section)); \
    LUS::BinaryWriter tmp = LUS::BinaryWriter(); \
    tmp.SetEndianness(LUS::Endianness::Big);     \

#define START_CUSTOM_SECTION(section) \
    {                                 \
    LUS::BinaryWriter tmp = LUS::BinaryWriter(); \
    tmp.SetEndianness(LUS::Endianness::Big);     \
    out.Write((uint32_t) BSWAP32(AIFC::MagicValues::AAPL)); \
    tmp.Write(AIFC::MagicValues::stoc); \
    tmp.Write(section, false);                 \

#define END_SECTION() \
    auto odata = tmp.ToVector(); \
    size_t size = odata.size();  \
    len += ALIGN(size, 2) + 8;   \
    out.Write((uint32_t) BSWAP32((uint32_t) size)); \
    out.Write(odata.data(), odata.size()); \
    if(size % 2){                \
        out.WriteByte(0);        \
    }                            \
    }                            \

void AudioManager::write_aifc(AudioBankSample* entry, LUS::BinaryWriter &out) {
    int16_t num_channels = 1;
    auto data = entry->data;
    size_t len = 0;
    assert(data.size() % 9 == 0);
    if(data.size() % 2 == 1){
        data.push_back('\0');
    }
    uint32_t num_frames = data.size() * 16 / 9;
    int16_t sample_size = 16;

    uint32_t sample_rate = -1;
    if(entry->tunings.size() == 1){
        sample_rate = 32000 * entry->tunings[0];
    } else {
        float tmin = PyUtils::min(entry->tunings);
        float tmax = PyUtils::max(entry->tunings);

        if(tmin <= 0.5f <= tmax){
            sample_rate = 16000;
        } else if(tmin <= 1.0f <= tmax){
            sample_rate = 32000;
        } else if(tmin <= 1.5f <= tmax){
            sample_rate = 48000;
        } else if(tmin <= 2.5f <= tmax){
            sample_rate = 80000;
        } else {
            sample_rate = 16000 * (tmin + tmax);
        }
    }

    out.Write((uint32_t) BSWAP32(AIFC::MagicValues::FORM));
    // This should be where the size is, but we need to write it later
    out.Write((uint32_t) 0);
    out.Write((uint32_t) BSWAP32(AIFC::MagicValues::AIFC));

    START_SECTION(AIFC::MagicValues::COMM);

    tmp.Write((uint16_t) num_channels);
    tmp.Write((uint32_t) num_frames);

    tmp.Write((uint16_t) sample_size);
    serialize_f80(sample_rate, tmp);
    tmp.Write(AIFC::MagicValues::VAPC);
    tmp.Write("\x0bVADPCM ~4-1", false);

    END_SECTION();

    START_SECTION(AIFC::MagicValues::INST)
    tmp.Write(std::string(20, '\0'), false);
    END_SECTION();

    START_CUSTOM_SECTION("\x0bVADPCMCODES")
    tmp.Write((uint16_t) 1);
    tmp.Write((uint16_t) entry->book.order);
    tmp.Write((uint16_t) entry->book.npredictors);

    for(auto x : entry->book.table){
        tmp.Write((int16_t) x);
    }
    END_SECTION();

    START_SECTION(AIFC::MagicValues::SSND)
    uint32_t zero = 0;
    tmp.Write((char*) &zero, 4);
    tmp.Write((char*) &zero, 4);
    tmp.Write((char*) data.data(), data.size());
    END_SECTION();

    if(entry->loop.count != 0){
        START_CUSTOM_SECTION("\x0bVADPCMLOOPS")
        uint16_t one = BSWAP16(1);
        tmp.Write(reinterpret_cast<char*>(&one), 2);
        tmp.Write(reinterpret_cast<char*>(&one), 2);
        tmp.Write(entry->loop.start);
        tmp.Write(entry->loop.end);
        tmp.Write(entry->loop.count);
        for(size_t i = 0; i < 16; i++){
            int16_t loop = BSWAP16(entry->loop.state.value()[i]);
            tmp.Write(reinterpret_cast<char*>(&loop), 2);
        }
        END_SECTION();
    }

    len += 4;
    out.Seek(4, LUS::SeekOffsetType::Start);
    out.Write((uint32_t) BSWAP32(len));

}

void AudioManager::bind_sample(YAML::Node& node, const std::string& path){
    auto id = GetSafeNode<uint32_t>(node, "id");
    sample_table[id] = path;
}

std::string& AudioManager::get_sample(uint32_t id) {
    if(!sample_table.contains(id)) {
        throw std::runtime_error("Failed to find sample with id " + std::to_string(id));
    }
    return sample_table[id];
}

void AudioManager::create_aifc(int32_t index, LUS::BinaryWriter &out) {
    int32_t idx = -1;
    for(auto &sample_bank : this->loaded_tbl.banks){
        auto offsets = PyUtils::keys(sample_bank->entries);
        std::sort(offsets.begin(), offsets.end());

        for(auto &offset : offsets){
            if(++idx == index){
                write_aifc(sample_bank->entries[offset], out);
                return;
            }
        }
    }
}

AudioBankSample AudioManager::get_aifc(int32_t index) {
    int32_t idx = -1;
    for(auto &sample_bank : this->loaded_tbl.banks){
        auto offsets = PyUtils::keys(sample_bank->entries);
        std::sort(offsets.begin(), offsets.end());

        for(auto &offset : offsets){
            if(++idx == index){
                return *sample_bank->entries[offset];
            }
        }
    }

    SPDLOG_ERROR("Invalid Index {}", index);
    throw std::runtime_error("Invalid index");
}

uint32_t AudioManager::get_index(AudioBankSample* entry) {
    if(!this->sampleMap.contains(entry)){
        return -1;
    }
    return this->sampleMap[entry];
}

std::map<uint32_t, Bank> AudioManager::get_banks() {
    return this->banks;
}

std::vector<AudioBankSample*> AudioManager::get_samples() {
    std::vector<AudioBankSample*> samples;
    for(auto &bank : this->loaded_tbl.banks){
        for(auto &entry : bank->entries){
            // Avoid duplicates
            if(std::find(samples.begin(), samples.end(), entry.second) == samples.end()){
                samples.push_back(entry.second);
            }
        }
    }
    return samples;
}