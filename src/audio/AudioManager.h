#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>

#define NONE 0xFFFF
#define ALIGN(val, al) (size_t) ((val + (al - 1)) & -al)

struct Entry {
    uint32_t offset;
    uint32_t length;
};

struct Sound {
    uint32_t offset;
    float tuning;
    bool operator==(const Sound& other) const {
        return offset == other.offset && tuning == other.tuning;
    }
};

struct Inst {
    std::string name;
    uint32_t offset;
    uint8_t releaseRate;
    uint8_t normalRangeLo;
    uint8_t normalRangeHi;
    uint32_t envelope;
    std::optional<Sound> soundLo;
    std::optional<Sound> soundMed;
    std::optional<Sound> soundHi;
};

struct Book {
    uint32_t order{};
    uint32_t npredictors{};
    std::vector<short> table;
    bool operator==(const Book& other) const {
        return order == other.order && npredictors == other.npredictors && table == other.table;
    }
};

struct Drum {
    std::string name;
    uint32_t offset;
    uint8_t releaseRate;
    uint8_t pan;
    uint32_t envelope;
    Sound sound;
    bool operator==(const Drum& other) const {
        return name == other.name && offset == other.offset && releaseRate == other.releaseRate && pan == other.pan && envelope == other.envelope && sound == other.sound;
    }
};

struct Loop {
    uint32_t start{};
    uint32_t end{};
    int32_t count{};
    uint32_t pad{};
    std::optional<std::vector<short>> state;
    bool operator==(const Loop& other) const {
        return start == other.start && end == other.end && count == other.count && pad == other.pad && state == other.state;
    }
};

struct AifcEntry {
    std::string name;
    std::vector<uint8_t> data;
    Book book;
    Loop loop;
    std::vector<float> tunings;
};

struct SampleBank {
    std::string name;
    uint32_t offset;
    std::vector<uint8_t> data;
    std::unordered_map<uint32_t, AifcEntry> entries;

    AifcEntry AddSample(uint32_t addr, size_t sampleSize, const Book& book, const Loop& loop);
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

typedef std::pair<unsigned short, unsigned short> EnvelopeData;

struct Envelope {
    std::string name;
    std::vector<EnvelopeData> entries;
};

struct Bank {
    std::string name;
    SampleBank sampleBank;
    std::vector<Inst> insts;
    std::vector<Drum> drums;
    std::vector<std::variant<Inst, std::vector<Drum>>> allInsts;
    std::vector<uint32_t> instOffsets;
    std::unordered_map<uint32_t, Envelope> envelopes;
    std::unordered_map<uint32_t, AifcEntry> samples;
    void print() const;
};

class AudioManager {
public:
    static AudioManager* Instance;
    void initialize(std::vector<uint8_t>& buffer);

private:
    static std::vector<Entry> parse_seq_file(std::vector<uint8_t>& buffer, uint32_t offset, bool isCTL);
    static CTLHeader parse_ctl_header(std::vector<uint8_t>& data);
    static std::optional<Sound> parse_sound(std::vector<uint8_t> data);
    static Drum parse_drum(std::vector<uint8_t>& data, uint32_t addr);
    static Inst parse_inst(std::vector<uint8_t>& data, uint32_t addr);
    static Loop parse_loop(uint32_t addr, std::vector<uint8_t>& bankData);
    static Book parse_book(uint32_t addr, std::vector<uint8_t>& bankData);
    static AifcEntry parse_sample(std::vector<uint8_t>& data, std::vector<uint8_t>& bankData, SampleBank& sampleBank);
    static std::vector<EnvelopeData> parse_envelope(uint32_t addr, std::vector<uint8_t>& dataBank);
    static Bank parse_ctl(CTLHeader header, std::vector<uint8_t> data, SampleBank bank, uint32_t index);
    static TBLFile parse_tbl(std::vector<uint8_t>& data, std::vector<Entry>& entries);

    std::vector<Bank> banks;
};