#pragma once

#include <factories/BaseFactory.h>
#include <unordered_set>

namespace FZX {

typedef struct TunedSample {
    std::string sampleRef;
    float tuning;
} TunedSample;

typedef struct Drum {
    uint8_t adsrDecayIndex;
    uint8_t pan;
    uint8_t isRelocated;
    TunedSample tunedSample;
    std::string envelopeRef;
} Drum;

typedef struct SoundEffect {
    TunedSample tunedSample;
} SoundEffect;

typedef struct Instrument {
    uint8_t isRelocated;
    uint8_t normalRangeLo;
    uint8_t normalRangeHi;
    uint8_t adsrDecayIndex;
    std::string envelopeRef;
    TunedSample lowPitchTunedSample;
    TunedSample normalPitchTunedSample;
    TunedSample highPitchTunedSample;
} Instrument;

typedef struct EnvelopePoint {
    int16_t delay;
    int16_t arg;
} EnvelopePoint;

typedef struct Sample {
    uint32_t codec;
    uint32_t medium;
    uint32_t unk_bit26;
    uint32_t isRelocated;
    uint32_t size;
    uint32_t rawSampleOffset;
    std::string loopRef;
    std::string bookRef;
} Sample;

typedef struct AdpcmBook {
    int32_t order;
    int32_t numPredictors;
    std::vector<int16_t> book;
} AdpcmBook;

typedef struct AdpcmLoop {
    uint32_t start;
    uint32_t end;
    uint32_t count;
    std::vector<int16_t> predictorState;
} AdpcmLoop;

enum class DataType {
    Drum,
    Sfx,
    Instrument,
    Envelope,
    Sample,
    Book,
    Loop,
    DrumOffsets,
};

typedef std::variant<Drum, std::vector<SoundEffect>, Instrument, std::vector<EnvelopePoint>, Sample, AdpcmBook, AdpcmLoop, std::vector<std::string>> SoundFontType;

typedef struct SoundFontEntry {
    std::string name;
    DataType type;
    SoundFontType data;
} SoundFontEntry;

class SoundFontData : public IParsedData {
public:

    std::vector<SoundFontEntry> mEntries;
    bool mSupportSfx;

    SoundFontData(std::vector<SoundFontEntry> entries, bool supportSfx) : mEntries(std::move(entries)), mSupportSfx(supportSfx) {}
};

class SoundFontHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SoundFontBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SoundFontCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SoundFontFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, SoundFontCodeExporter)
            REGISTER(Header, SoundFontHeaderExporter)
            REGISTER(Binary, SoundFontBinaryExporter)
        };
    }
private:
    std::string RegisterSoundFontData(std::string symbol, FZX::DataType dataType, uint32_t offset, std::map<uint32_t, std::pair<FZX::DataType, std::string>>& dataMap, std::unordered_map<FZX::DataType, uint32_t>& dataCountMap);
};
}
