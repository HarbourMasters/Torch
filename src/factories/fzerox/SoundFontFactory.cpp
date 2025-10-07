#include "SoundFontFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

static std::unordered_map<FZX::DataType, std::string> sSoundFontDataTypeMap = {
    { FZX::DataType::Drum, "Drum" },
    { FZX::DataType::Sfx, "Sfx" },
    { FZX::DataType::Instrument, "Instrument" },
    { FZX::DataType::Envelope, "Envelope" },
    { FZX::DataType::Sample, "Sample" },
    { FZX::DataType::Book, "Book" },
    { FZX::DataType::Loop, "Loop" },
    { FZX::DataType::DrumOffsets, "DrumOffsets" },
};

typedef enum {
    /* 0 */ CODEC_ADPCM,
    /* 1 */ CODEC_S8,
    /* 2 */ CODEC_S16_INMEMORY,
    /* 3 */ CODEC_SMALL_ADPCM,
    /* 4 */ CODEC_REVERB,
    /* 5 */ CODEC_S16,
    /* 6 */ CODEC_UNK6
} SampleCodec;

typedef enum {
    /* 0 */ MEDIUM_RAM,
    /* 1 */ MEDIUM_LBA,
    /* 2 */ MEDIUM_CART,
    /* 3 */ MEDIUM_DISK_DRIVE
} SampleMedium;

static std::unordered_map<uint32_t, std::string> sCodecMap = {
    { CODEC_ADPCM, "CODEC_ADPCM" },
    { CODEC_S8, "CODEC_S8" },
    { CODEC_S16_INMEMORY, "CODEC_S16_INMEMORY" },
    { CODEC_SMALL_ADPCM, "CODEC_SMALL_ADPCM" },
    { CODEC_REVERB, "CODEC_REVERB" },
    { CODEC_S16, "CODEC_S16" },
    { CODEC_UNK6, "CODEC_UNK6" },
};

static std::unordered_map<uint32_t, std::string> sMediumMap = {
    { MEDIUM_RAM, "MEDIUM_RAM" },
    { MEDIUM_LBA, "MEDIUM_LBA" },
    { MEDIUM_CART, "MEDIUM_CART" },
    { MEDIUM_DISK_DRIVE, "MEDIUM_DISK_DRIVE" },
};

#define FORMAT_HEX(x, w) "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(w) << x << std::nouppercase << std::dec
#define FORMAT_FLOAT(x, w, p) std::dec << std::setfill(' ') << std::fixed << std::setprecision(p) << std::setw(w) << x << "f"

#define DRUM_SIZE 0x10
#define SFX_SIZE 0x8
#define INSTRUMENT_SIZE 0x20
#define ENVELOPE_SIZE 0x4
#define SAMPLE_SIZE 0x10
#define BOOK_HEADER_SIZE 0x8
#define LOOP_SIZE 0x10
#define LOOP_SIZE2 0x30
#define ALIGN16(val) (((val) + 0xF) & ~0xF)

ExportResult FZX::SoundFontHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto soundFontData = std::static_pointer_cast<SoundFontData>(raw);
    
    write << "extern u32 " << symbol << "Offsets[];\n";

    for (const auto& entry : soundFontData->mEntries) {
        switch (entry.type) {
            case DataType::Drum:
                write << "extern Drum " << entry.name << "[];\n";
                break;
            case DataType::Sfx:
                write << "extern SoundEffect " << entry.name << "[];\n";
                break;
            case DataType::Instrument:
                write << "extern Instrument " << entry.name << ";\n";
                break;
            case DataType::Envelope:
                write << "extern EnvelopePoint " << entry.name << "[];\n";
                break;
            case DataType::Sample:
                write << "extern Sample " << entry.name << ";\n";
                break;
            case DataType::Book:
                write << "extern AdpcmBook " << entry.name << ";\n";
                break;
            case DataType::Loop: {
                auto loop = std::get<AdpcmLoop>(entry.data);
                if (loop.count != 0) {
                    write << "extern AdpcmLoop " << entry.name << ";\n";
                } else {
                    write << "extern AdpcmLoopHeader " << entry.name << ";\n";
                }
                break;
            }
            case DataType::DrumOffsets:
                write << "extern u32 " << entry.name << "[];\n";
                break;
        }
    }

    write << "\n";

    return std::nullopt;
}

ExportResult FZX::SoundFontCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto soundFontData = std::static_pointer_cast<SoundFontData>(raw);

    // Calculate offsets
    std::unordered_map<std::string, uint32_t> dataNameToOffsetMap;
    std::unordered_map<std::string, uint32_t> dataNameToSizeMap;
    uint32_t rollingOffset = 0;

    rollingOffset += sizeof(uint32_t);
    if (soundFontData->mSupportSfx) {
        rollingOffset += sizeof(uint32_t);
    }
    bool hasDrums = false;
    bool hasSfx = false;
    std::string drumOffsetName;
    std::string sfxName;
    std::vector<std::string> instrumentNameList;
    for (const auto& entry : soundFontData->mEntries) {
        if (entry.type == DataType::DrumOffsets) {
            drumOffsetName = entry.name;
            hasDrums = true;
            continue;
        }
        if (entry.type == DataType::Sfx) {
            sfxName = entry.name;
            hasSfx = true;
            continue;
        }
        if (entry.type != DataType::Instrument) {
            continue;
        }

        instrumentNameList.push_back(entry.name);
        rollingOffset += sizeof(uint32_t);
    }

    for (const auto& entry : soundFontData->mEntries) {
        uint32_t size;
        rollingOffset = ALIGN16(rollingOffset);

        dataNameToOffsetMap[entry.name] = rollingOffset;

        switch (entry.type) {
            case DataType::Drum:
                size = DRUM_SIZE;
                break;
            case DataType::Sfx:
                size = SFX_SIZE * std::get<std::vector<SoundEffect>>(entry.data).size();
                break;
            case DataType::Instrument:
                size = INSTRUMENT_SIZE;
                break;
            case DataType::Envelope:
                size = ENVELOPE_SIZE * std::get<std::vector<EnvelopePoint>>(entry.data).size();
                break;
            case DataType::Sample:
                size = SAMPLE_SIZE;
                break;
            case DataType::Book: {
                auto book = std::get<AdpcmBook>(entry.data);
                size = BOOK_HEADER_SIZE + book.order * book.numPredictors * 8 * sizeof(uint16_t);
                break;
            }
            case DataType::Loop: {
                auto loop = std::get<AdpcmLoop>(entry.data);
                if (loop.count != 0) {
                    size = LOOP_SIZE2;
                } else {
                    size = LOOP_SIZE;
                }
                break;
            }
            case DataType::DrumOffsets:
                size = sizeof(uint32_t) * std::get<std::vector<std::string>>(entry.data).size();
                break;
        }

        dataNameToSizeMap[entry.name] = size;
        SPDLOG_INFO("ENTRY OUT {} {}, 0x{:X} (size 0x{:X})", sSoundFontDataTypeMap[entry.type], entry.name, rollingOffset, size);
        rollingOffset += size;
    }
    uint32_t totalSize = rollingOffset;

    // Write Out
    rollingOffset = 0;

    write << "u32 " << symbol + "Offsets[] = {\n";

    write << fourSpaceTab;

    if (hasDrums) {
        write << FORMAT_HEX(dataNameToOffsetMap[drumOffsetName], 4) << ",";
    } else {
        write << "0,";
    }
    rollingOffset += sizeof(uint32_t);

    if (soundFontData->mSupportSfx) {
        if (hasSfx) {
            write << " " << FORMAT_HEX(dataNameToOffsetMap[sfxName], 4) << ",";
        } else {
            write << " 0,";
        }
        rollingOffset += sizeof(uint32_t);
    }

    uint32_t instrumentCount = 0;
    for (const auto& instName : instrumentNameList) {
        if (instrumentCount % 6 == 0) {
            write << "\n" << fourSpaceTab;
        } else {
            write << " ";
        }

        write << FORMAT_HEX(dataNameToOffsetMap[instName], 4) << ",";
        instrumentCount++;
        rollingOffset += sizeof(uint32_t);
    }

    write << "\n};\n\n";

    static uint32_t padCount = 0;
    for (const auto& entry : soundFontData->mEntries) {
        if (rollingOffset % 0x10 != 0) {
            uint32_t paddingSize = 0x10 - (rollingOffset % 0x10);
            write << "static u8 padding" << (padCount++) << "[" << FORMAT_HEX(paddingSize, 1) << "] = { 0 };\n\n";
        }
        rollingOffset = ALIGN16(rollingOffset);

        write << "/* Reference Offset " << FORMAT_HEX(rollingOffset, 1) << " */\n";
        switch (entry.type) {
            case DataType::Drum: {
                auto drum = std::get<Drum>(entry.data);
                write << "Drum " << entry.name << " = {\n";
                write << fourSpaceTab << (uint32_t)drum.adsrDecayIndex << ", " << (uint32_t)drum.pan << ", 0,\n";
                write << fourSpaceTab << "{ " << FORMAT_HEX(dataNameToOffsetMap[drum.tunedSample.sampleRef], 1) << ", " << FORMAT_FLOAT(drum.tunedSample.tuning, 7, 7) << " },\n";
                write << fourSpaceTab << FORMAT_HEX(dataNameToOffsetMap[drum.envelopeRef], 1) << ",\n";
                write << "};\n\n";
                break;
            }
            case DataType::Sfx: {
                auto sfxList = std::get<std::vector<SoundEffect>>(entry.data);
                write << "SoundEffect " << entry.name << "[] = {\n";
                for (const auto& sfx : sfxList) {
                    write << fourSpaceTab << "{ { " << FORMAT_HEX(dataNameToOffsetMap[sfx.tunedSample.sampleRef], 1) << ", " << FORMAT_FLOAT(sfx.tunedSample.tuning, 7, 7) << " } },\n";
                }
                write << "};\n\n";
                break;
            }
            case DataType::Instrument: {
                auto instrument = std::get<Instrument>(entry.data);
                write << "Instrument " << entry.name << " = {\n";
                write << fourSpaceTab << "0, " << (uint32_t)instrument.normalRangeLo << ", " << (uint32_t)instrument.normalRangeHi << ", " << (uint32_t)instrument.adsrDecayIndex << ", " << FORMAT_HEX(dataNameToOffsetMap[instrument.envelopeRef], 1) << ",\n";
                write << fourSpaceTab << "{ " << FORMAT_HEX(dataNameToOffsetMap[instrument.lowPitchTunedSample.sampleRef], 1) << ", " << FORMAT_FLOAT(instrument.lowPitchTunedSample.tuning, 7, 7) << " },\n";
                write << fourSpaceTab << "{ " << FORMAT_HEX(dataNameToOffsetMap[instrument.normalPitchTunedSample.sampleRef], 1) << ", " << FORMAT_FLOAT(instrument.normalPitchTunedSample.tuning, 7, 7) << " },\n";
                write << fourSpaceTab << "{ " << FORMAT_HEX(dataNameToOffsetMap[instrument.highPitchTunedSample.sampleRef], 1) << ", " << FORMAT_FLOAT(instrument.highPitchTunedSample.tuning, 7, 7) << " },\n";
                write << "};\n\n";
                break;
            }
            case DataType::Envelope: {
                auto envelopes = std::get<std::vector<EnvelopePoint>>(entry.data);
                write << "EnvelopePoint " << entry.name << "[] = {\n";
                for (const auto envelope : envelopes) {
                    write << fourSpaceTab << "{ " << envelope.delay << ", " << envelope.arg << " },\n";
                }
                write << "};\n\n";
                break;
            }
            case DataType::Sample: {
                auto sample = std::get<Sample>(entry.data);
                write << "Sample " << entry.name << " = {\n";

                write << fourSpaceTab;
                // TODO: THIS IS WRONG, ADD SEPARATE FIELD FOR THIS AUDIO SYSTEM UPDATE
                if (soundFontData->mSupportSfx) {
                    write << "0, ";
                }
                write << sCodecMap.at(sample.codec) << ", " << sMediumMap.at(sample.medium) << ", " << sample.unk_bit26 << ", 0, " << FORMAT_HEX(sample.size, 1) << ",\n";
                write << fourSpaceTab << FORMAT_HEX(sample.rawSampleOffset, 1) << ", " << FORMAT_HEX(dataNameToOffsetMap[sample.loopRef], 1) << ", " << FORMAT_HEX(dataNameToOffsetMap[sample.bookRef], 1);

                write << "\n};\n\n";
                break;
            }
            case DataType::Book: {
                auto book = std::get<AdpcmBook>(entry.data);
                write << "AdpcmBook " << entry.name << " = {\n";

                write << fourSpaceTab << "{ " << book.order << ", " << book.numPredictors << " },";

                uint32_t bookCount = 0;
                for (const auto& bookData : book.book) {
                    if (bookCount % 6 == 0) {
                        write << "\n" << fourSpaceTab;
                    } else {
                        write << " ";
                    }

                    write << FORMAT_HEX(bookData, 4) << ",";
                    bookCount++;
                }
                write << "\n};\n\n";
                break;
            }
            case DataType::Loop: {
                auto loop = std::get<AdpcmLoop>(entry.data);
                if (loop.count != 0) {
                    write << "AdpcmLoop " << entry.name << " = {\n";
                    write << fourSpaceTab << "{ " << loop.start << ", " << loop.end << ", " << FORMAT_HEX(loop.count, 1) << ", { 0 } },";
                } else {
                    write << "AdpcmLoopHeader " << entry.name << " = {\n";
                    write << fourSpaceTab << loop.start << ", " << loop.end << ", " << FORMAT_HEX(loop.count, 1) << ", { 0 },";
                }

                if (loop.count != 0) {
                    uint32_t predictorCount = 0;
                    for (const auto& predictorState : loop.predictorState) {
                        if (predictorCount % 6 == 0) {
                            write << "\n" << fourSpaceTab;
                        } else {
                            write << " ";
                        }

                        write << FORMAT_HEX(predictorState, 4) << ",";
                        predictorCount++;
                    }
                }

                write << "\n};\n\n";
                break;
            }
            case DataType::DrumOffsets: {
                auto drumOffsetListNames = std::get<std::vector<std::string>>(entry.data);
                write << "u32 " << entry.name << "[] = {\n";

                uint32_t drumCount = 0;
                for (const auto& drumName : drumOffsetListNames) {
                    if (drumCount % 6 == 0) {
                        write << "\n" << fourSpaceTab;
                    } else {
                        write << " ";
                    }

                    write << FORMAT_HEX(dataNameToOffsetMap[drumName], 4) << ",";
                    drumCount++;
                }

                write << "\n};\n\n";
                break;
            }
        }

        rollingOffset += dataNameToSizeMap[entry.name];
    }

    return offset + totalSize;
}

ExportResult FZX::SoundFontBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {

    return std::nullopt;
}

std::string FZX::SoundFontFactory::RegisterSoundFontData(std::string symbol, FZX::DataType dataType, uint32_t offset, std::map<uint32_t, std::pair<FZX::DataType, std::string>>& dataMap, std::unordered_map<FZX::DataType, uint32_t>& dataCountMap) {
    std::string dataName;

    if (dataMap.contains(offset)) {
        return dataMap.at(offset).second;
    }

    switch (dataType) {
        case FZX::DataType::Drum: {
            dataName = symbol + "Drum" + std::to_string(dataCountMap[dataType]++);
            break;
        }
        case FZX::DataType::Sfx: {
            dataName = symbol + "Sfx";
            break;
        }
        case FZX::DataType::Instrument: {
            dataName = symbol + "Instrument" + std::to_string(dataCountMap[dataType]++);
            break;
        }
        case FZX::DataType::Sample: {
            dataName = symbol + "Sample" + std::to_string(dataCountMap[dataType]++);
            break;
        }
        case FZX::DataType::Envelope: {
            dataName = symbol + "Envelope" + std::to_string(dataCountMap[dataType]++);
            break;
        }
        case FZX::DataType::Book: {
            dataName = symbol + "Book" + std::to_string(dataCountMap[dataType]++);
            break;
        }
        case FZX::DataType::Loop: {
            dataName = symbol + "Loop" + std::to_string(dataCountMap[dataType]++);
            break;
        }
        case FZX::DataType::DrumOffsets: {
            dataName = symbol + "DrumOffsets";
            break;
        }
        default:
            throw std::runtime_error("Invalid SoundFont Data Type");
    }

    dataMap[offset] = std::make_pair(dataType, dataName);

    return dataName;
}

std::optional<std::shared_ptr<IParsedData>> FZX::SoundFontFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    LUS::BinaryReader reader(segment.data, segment.size);

    reader.SetEndianness(Torch::Endianness::Big);

    std::map<uint32_t, std::pair<DataType, std::string>> dataMap;
    std::unordered_map<FZX::DataType, uint32_t> dataCountMap;

    const auto numDrums = GetSafeNode<uint32_t>(node, "num_drums");
    const auto numInstruments = GetSafeNode<uint32_t>(node, "num_instruments");
    const auto supportSfx = GetSafeNode<bool>(node, "support_sfx");

    uint32_t numSfx = 0;
    if (supportSfx) {
        numSfx = GetSafeNode<uint32_t>(node, "num_sfx");
    }

    std::unordered_map<std::string, SoundFontType> soundFontMap;
    uint32_t drumOffsetListOffset = reader.ReadUInt32();
    if (numDrums > 0) {
        RegisterSoundFontData(symbol, DataType::DrumOffsets, drumOffsetListOffset, dataMap, dataCountMap);
    }
    uint32_t sfxOffset = 0;
    if (supportSfx) {
        sfxOffset = reader.ReadUInt32();
        if (numSfx > 0) {
            RegisterSoundFontData(symbol, DataType::Sfx, sfxOffset, dataMap, dataCountMap);
        }
    }

    std::vector<uint32_t> instrumentOffsets;
    for (uint32_t i = 0; i < numInstruments; i++) {
        auto instrumentOffset = reader.ReadUInt32();
        RegisterSoundFontData(symbol, DataType::Instrument, instrumentOffset, dataMap, dataCountMap);
        instrumentOffsets.push_back(instrumentOffset);
    }

    std::unordered_set<uint32_t> sampleOffsets;
    std::unordered_set<uint32_t> envelopeOffsets;

    // Parse Drums
    std::vector<Drum> drums;
    SPDLOG_INFO("FZX SOUNDFONT SEEK drumOffsetList 0x{:X}", drumOffsetListOffset);
    reader.Seek(drumOffsetListOffset, LUS::SeekOffsetType::Start);

    std::unordered_set<uint32_t> drumOffsets;
    if (numDrums > 0) {
        std::vector<std::string> drumRefList;

        for (uint32_t i = 0; i < numDrums; i++) {
            auto drumOffset = reader.ReadUInt32();
            drumRefList.push_back(RegisterSoundFontData(symbol, DataType::Drum, drumOffset, dataMap, dataCountMap));
            drumOffsets.insert(drumOffset);
        }
        soundFontMap[dataMap.at(drumOffsetListOffset).second] = drumRefList;
    }

    for (const auto& drumOffset : drumOffsets) {
        SPDLOG_INFO("FZX SOUNDFONT SEEK drum 0x{:X}", drumOffset);
        reader.Seek(drumOffset, LUS::SeekOffsetType::Start);
        Drum drum;
        drum.adsrDecayIndex = reader.ReadUByte();
        drum.pan = reader.ReadUByte();
        drum.isRelocated = reader.ReadUByte();
        reader.ReadUByte();
        
        auto sampleOffset = reader.ReadUInt32();
        drum.tunedSample.sampleRef = RegisterSoundFontData(symbol, DataType::Sample, sampleOffset, dataMap, dataCountMap);
        sampleOffsets.insert(sampleOffset);
        drum.tunedSample.tuning = reader.ReadFloat();
        auto envelopeOffset = reader.ReadUInt32();
        drum.envelopeRef = RegisterSoundFontData(symbol, DataType::Envelope, envelopeOffset, dataMap, dataCountMap);
        envelopeOffsets.insert(envelopeOffset);

        soundFontMap[dataMap.at(drumOffset).second] = drum;
    }

    // Parse Sfx
    if (supportSfx) {
        SPDLOG_INFO("FZX SOUNDFONT SEEK sfx 0x{:X}", sfxOffset);
        reader.Seek(sfxOffset, LUS::SeekOffsetType::Start);
        
        if (numSfx > 0) {
            std::vector<SoundEffect> soundEffects;
            for (uint32_t i = 0; i < numSfx; i++) {
                SoundEffect soundEffect;
                auto sampleOffset = reader.ReadUInt32();
                soundEffect.tunedSample.sampleRef = RegisterSoundFontData(symbol, DataType::Sample, sampleOffset, dataMap, dataCountMap);
                sampleOffsets.insert(sampleOffset);
                soundEffect.tunedSample.tuning = reader.ReadFloat();
                soundEffects.push_back(soundEffect);
            }
   
            soundFontMap[dataMap.at(sfxOffset).second] = soundEffects;
        }
    }

    // Parse Instruments
    for (const auto& instrumentOffset : instrumentOffsets) {
        Instrument instrument;
        uint32_t sampleOffset;
        SPDLOG_INFO("FZX SOUNDFONT SEEK instrument 0x{:X}", instrumentOffset);
        reader.Seek(instrumentOffset, LUS::SeekOffsetType::Start);

        instrument.isRelocated = reader.ReadUByte();
        instrument.normalRangeLo = reader.ReadUByte();
        instrument.normalRangeHi = reader.ReadUByte();
        instrument.adsrDecayIndex = reader.ReadUByte();
        auto envelopeOffset = reader.ReadUInt32();
        instrument.envelopeRef = RegisterSoundFontData(symbol, DataType::Envelope, envelopeOffset, dataMap, dataCountMap);
        envelopeOffsets.insert(envelopeOffset);

        sampleOffset = reader.ReadUInt32();
        if (sampleOffset != 0) {
            instrument.lowPitchTunedSample.sampleRef = RegisterSoundFontData(symbol, DataType::Sample, sampleOffset, dataMap, dataCountMap);
            sampleOffsets.insert(sampleOffset);
        }
        instrument.lowPitchTunedSample.tuning = reader.ReadFloat();

        sampleOffset = reader.ReadUInt32();
        if (sampleOffset != 0) {
            instrument.normalPitchTunedSample.sampleRef = RegisterSoundFontData(symbol, DataType::Sample, sampleOffset, dataMap, dataCountMap);
            sampleOffsets.insert(sampleOffset);
        }
        instrument.normalPitchTunedSample.tuning = reader.ReadFloat();

        sampleOffset = reader.ReadUInt32();
        if (sampleOffset != 0) {
            instrument.highPitchTunedSample.sampleRef = RegisterSoundFontData(symbol, DataType::Sample, sampleOffset, dataMap, dataCountMap);
            sampleOffsets.insert(sampleOffset);
        }
        instrument.highPitchTunedSample.tuning = reader.ReadFloat();

        soundFontMap[dataMap.at(instrumentOffset).second] = instrument;
    }

    // Parse Envelopes
    for (const auto& envelopeOffset : envelopeOffsets) {
        std::vector<EnvelopePoint> envelopes;
        SPDLOG_INFO("FZX SOUNDFONT SEEK envelope 0x{:X}", envelopeOffset);
        reader.Seek(envelopeOffset, LUS::SeekOffsetType::Start);
        while (true) {
            EnvelopePoint envelope;
            envelope.delay = reader.ReadInt16();
            envelope.arg = reader.ReadInt16();
            envelopes.push_back(envelope);

            if (envelope.delay <= 0) {
                break;
            }
        }
        soundFontMap[dataMap.at(envelopeOffset).second] = envelopes;
    }

    // Parse Samples
    std::unordered_set<uint32_t> loopOffsets;
    std::unordered_set<uint32_t> bookOffsets;
    for (const auto& sampleOffset : sampleOffsets) {
        Sample sample;
        SPDLOG_INFO("FZX SOUNDFONT SEEK sample 0x{:X}", sampleOffset);
        reader.Seek(sampleOffset, LUS::SeekOffsetType::Start);
        
        uint32_t sampleBitfield = reader.ReadUInt32();
        sample.codec = (sampleBitfield & (0b0111 << 28)) >> 28;
        sample.medium = (sampleBitfield & (0b11 << 26)) >> 26;
        sample.unk_bit26 = (sampleBitfield & (1 << 25)) >> 25;
        sample.isRelocated = (sampleBitfield & (1 << 24)) >> 24;
        sample.size = sampleBitfield & 0x00FFFFFF;

        sample.rawSampleOffset = reader.ReadUInt32();
        auto loopOffset = reader.ReadUInt32();
        sample.loopRef = RegisterSoundFontData(symbol, DataType::Loop, loopOffset, dataMap, dataCountMap);
        loopOffsets.insert(loopOffset);
        auto bookOffset = reader.ReadUInt32();
        sample.bookRef = RegisterSoundFontData(symbol, DataType::Book, bookOffset, dataMap, dataCountMap);
        bookOffsets.insert(bookOffset);
        
        soundFontMap[dataMap.at(sampleOffset).second] = sample;
    }

    // Parse Loops
    for (const auto& loopOffset : loopOffsets) {
        AdpcmLoop loop;
        SPDLOG_INFO("FZX SOUNDFONT SEEK loop 0x{:X}", loopOffset);
        reader.Seek(loopOffset, LUS::SeekOffsetType::Start);

        loop.start = reader.ReadUInt32();
        loop.end = reader.ReadUInt32();
        loop.count = reader.ReadUInt32();
        reader.ReadUInt32();

        if (loop.count != 0) {
            for (uint32_t i = 0; i < 16; i++) {
                loop.predictorState.push_back(reader.ReadInt16());
            }
        }
        soundFontMap[dataMap.at(loopOffset).second] = loop;
    }

    // Parse Books
    for (const auto& bookOffset : bookOffsets) {
        AdpcmBook book;
        SPDLOG_INFO("FZX SOUNDFONT SEEK book 0x{:X}", bookOffset);
        reader.Seek(bookOffset, LUS::SeekOffsetType::Start);

        book.order = reader.ReadInt32();
        book.numPredictors = reader.ReadInt32();

        for (int32_t i = 0; i < book.order * book.numPredictors * 8; i++) {
            book.book.push_back(reader.ReadInt16());
        }
        soundFontMap[dataMap.at(bookOffset).second] = book;
    }

    // Sort SoundFont Entries into Vector by offset
    std::vector<SoundFontEntry> soundFontEntries;
    for (const auto& [offset, dataInfoPair] : dataMap) {
        SoundFontEntry entry;
        entry.type = dataInfoPair.first;
        entry.name = dataInfoPair.second;
        entry.data = std::move(soundFontMap.at(entry.name));
        SPDLOG_INFO("ENTRY IN {} {}, 0x{:X}", sSoundFontDataTypeMap[entry.type], entry.name, offset);

        soundFontEntries.push_back(entry);
    }
    
    return std::make_shared<SoundFontData>(soundFontEntries, supportSfx);
}
