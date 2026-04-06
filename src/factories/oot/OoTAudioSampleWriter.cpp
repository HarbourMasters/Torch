#ifdef OOT_SUPPORT

#include "OoTAudioSampleWriter.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <iomanip>
#include <sstream>

namespace OoT {

std::map<int, std::map<int, std::string>> AudioSampleWriter::ParseSampleNames(YAML::Node& node) {
    std::map<int, std::map<int, std::string>> sampleNames;
    if (node["samples"] && node["samples"].IsSequence()) {
        auto samplesNode = node["samples"];
        for (size_t i = 0; i < samplesNode.size(); i++) {
            auto bankNode = samplesNode[i];
            int bank = bankNode["bank"].as<int>();
            if (bankNode["entries"] && bankNode["entries"].IsSequence()) {
                auto entries = bankNode["entries"];
                for (size_t j = 0; j < entries.size(); j++) {
                    auto e = entries[j];
                    sampleNames[bank][e["offset"].as<int>()] = e["name"].as<std::string>();
                }
            }
        }
    }
    return sampleNames;
}

void AudioSampleWriter::ParseSample(int bankIndex, uint32_t sampleAddr, uint32_t baseOffset, AudioParseContext& ctx) {
    if (sampleAddr + 16 > ctx.audioBank.Size()) return;

    uint32_t dataRelPtr = ctx.audioBank.ReadU32(sampleAddr + 4);
    uint32_t sampleDataOffset = dataRelPtr + ctx.sampleBankTable[bankIndex].ptr;
    if (ctx.sampleMap.count(sampleDataOffset)) return;

    SampleInfo s;
    uint32_t origField = ctx.audioBank.ReadU32(sampleAddr);
    s.codec = (origField >> 28) & 0x0F;
    s.medium = (origField >> 24) & 0x03;
    s.unk_bit26 = (origField >> 22) & 0x01;
    s.unk_bit25 = (origField >> 21) & 0x01;
    s.dataSize = origField & 0x00FFFFFF;
    s.dataOffset = sampleDataOffset;

    uint32_t loopAddr = ctx.audioBank.ReadU32(sampleAddr + 8) + baseOffset;
    uint32_t bookAddr = ctx.audioBank.ReadU32(sampleAddr + 12) + baseOffset;

    if (loopAddr + 12 <= ctx.audioBank.Size()) {
        s.loopStart = (int32_t)ctx.audioBank.ReadU32(loopAddr);
        s.loopEnd = (int32_t)ctx.audioBank.ReadU32(loopAddr + 4);
        s.loopCount = (int32_t)ctx.audioBank.ReadU32(loopAddr + 8);

        if (s.loopCount != 0 && loopAddr + 48 <= ctx.audioBank.Size()) {
            for (int i = 0; i < 16; i++) {
                s.loopStates.push_back(ctx.audioBank.ReadS16(loopAddr + 16 + i * 2));
            }
        }
    }

    if (bookAddr + 8 <= ctx.audioBank.Size()) {
        s.bookOrder = (int32_t)ctx.audioBank.ReadU32(bookAddr);
        s.bookNpredictors = (int32_t)ctx.audioBank.ReadU32(bookAddr + 4);
        int numBooks = s.bookOrder * s.bookNpredictors * 8;
        if (bookAddr + 8 + numBooks * 2 <= ctx.audioBank.Size()) {
            for (int i = 0; i < numBooks; i++) {
                s.books.push_back(ctx.audioBank.ReadS16(bookAddr + 8 + i * 2));
            }
        }
    }

    // Resolve name from YAML (use absolute offset as key, matching ZAPDTR behavior)
    if (ctx.sampleNames.count(bankIndex) && ctx.sampleNames[bankIndex].count((int)sampleDataOffset)) {
        s.name = ctx.sampleNames[bankIndex][(int)sampleDataOffset];
    } else {
        std::ostringstream ss;
        ss << "sample_" << bankIndex << "_" << std::setfill('0') << std::setw(8)
           << std::hex << std::uppercase << sampleDataOffset;
        s.name = ss.str();
    }

    ctx.sampleMap[sampleDataOffset] = s;
}

void AudioSampleWriter::ParseSFESample(int bankIndex, uint32_t sfeAddr, uint32_t baseOffset, AudioParseContext& ctx) {
    if (sfeAddr + 4 > ctx.audioBank.Size()) return;
    uint32_t samplePtr = ctx.audioBank.ReadU32(sfeAddr);
    if (samplePtr != 0) {
        ParseSample(bankIndex, samplePtr + baseOffset, baseOffset, ctx);
    }
}

void AudioSampleWriter::WriteCompanionFiles(const std::map<uint32_t, SampleInfo>& sampleMap,
                                             const std::vector<uint8_t>& audioTable) {
    for (auto& [offset, s] : sampleMap) {
        LUS::BinaryWriter w;
        BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudioSample, 2);

        w.Write(s.codec);
        w.Write(s.medium);
        w.Write(s.unk_bit26);
        w.Write(s.unk_bit25);
        w.Write(static_cast<uint32_t>(s.dataSize));
        if (s.dataOffset + s.dataSize <= audioTable.size()) {
            w.Write((char*)(audioTable.data() + s.dataOffset), s.dataSize);
        }

        w.Write(static_cast<uint32_t>(s.loopStart));
        w.Write(static_cast<uint32_t>(s.loopEnd));
        w.Write(static_cast<uint32_t>(s.loopCount));
        w.Write(static_cast<uint32_t>(s.loopStates.size()));
        for (auto ls : s.loopStates) w.Write(ls);

        w.Write(static_cast<uint32_t>(s.bookOrder));
        w.Write(static_cast<uint32_t>(s.bookNpredictors));
        w.Write(static_cast<uint32_t>(s.books.size()));
        for (auto b : s.books) w.Write(b);

        std::stringstream ss;
        w.Finish(ss);
        std::string str = ss.str();
        Companion::Instance->RegisterCompanionFile(
            "samples/" + s.name + "_META", std::vector<char>(str.begin(), str.end()));
    }

    SPDLOG_INFO("OoTAudioFactory: wrote {} sample companion files", sampleMap.size());
}

void AudioSampleWriter::DiscoverSamples(const std::vector<AudioTableEntry>& fontTable, AudioParseContext& ctx) {
    for (uint32_t fi = 0; fi < fontTable.size(); fi++) {
        auto& fe = fontTable[fi];
        uint32_t ptr = fe.ptr;
        int sampleBankId = (fe.data1 >> 8) & 0xFF;
        int numInstruments = (fe.data2 >> 8) & 0xFF;
        int numDrums = fe.data2 & 0xFF;
        int numSfx = fe.data3;

        if (ptr + 8 > ctx.audioBank.Size()) continue;

        // Drums
        uint32_t drumListAddr = ctx.audioBank.ReadU32(ptr) + ptr;
        for (int i = 0; i < numDrums; i++) {
            if (drumListAddr + (i + 1) * 4 > ctx.audioBank.Size()) break;
            uint32_t drumPtr = ctx.audioBank.ReadU32(drumListAddr + i * 4);
            if (drumPtr != 0) {
                drumPtr += ptr;
                if (drumPtr + 8 > ctx.audioBank.Size()) continue;
                uint32_t sampleEntryPtr = ctx.audioBank.ReadU32(drumPtr + 4) + ptr;
                ParseSample(sampleBankId, sampleEntryPtr, ptr, ctx);
            }
        }

        // SFX
        uint32_t sfxListAddr = ctx.audioBank.ReadU32(ptr + 4) + ptr;
        for (int i = 0; i < numSfx; i++) {
            if (sfxListAddr + (i + 1) * 8 > ctx.audioBank.Size()) break;
            ParseSFESample(sampleBankId, sfxListAddr + i * 8, ptr, ctx);
        }

        // Instruments
        for (int i = 0; i < numInstruments; i++) {
            if (ptr + 8 + (i + 1) * 4 > ctx.audioBank.Size()) break;
            uint32_t instPtr = ctx.audioBank.ReadU32(ptr + 8 + i * 4);
            if (instPtr != 0) {
                instPtr += ptr;
                if (instPtr + 28 > ctx.audioBank.Size()) continue;
                ParseSFESample(sampleBankId, instPtr + 8, ptr, ctx);
                ParseSFESample(sampleBankId, instPtr + 16, ptr, ctx);
                ParseSFESample(sampleBankId, instPtr + 24, ptr, ctx);
            }
        }
    }
}

bool AudioSampleWriter::Extract(std::vector<uint8_t>& buffer, YAML::Node& node,
                                SafeAudioBankReader& audioBank,
                                const std::vector<AudioTableEntry>& fontTable,
                                const std::vector<AudioTableEntry>& sampleBankTable,
                                std::map<uint32_t, SampleInfo>& sampleMap) {
    // Load Audiotable segment (only needed for sample data extraction)
    auto audiotableSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(3);
    if (!audiotableSeg.has_value()) {
        SPDLOG_ERROR("OoTAudioFactory: Audiotable segment not found");
        return false;
    }
    uint32_t tableOff = audiotableSeg.value();
    uint32_t tableSize = std::min((uint32_t)0x500000, (uint32_t)(buffer.size() - tableOff));
    std::vector<uint8_t> audioTable(buffer.begin() + tableOff, buffer.begin() + tableOff + tableSize);

    AudioParseContext ctx {
        .audioBank = audioBank,
        .sampleBankTable = sampleBankTable,
        .sampleNames = ParseSampleNames(node),
        .sampleMap = sampleMap,
    };

    DiscoverSamples(fontTable, ctx);

    SPDLOG_INFO("OoTAudioFactory: discovered {} unique samples", sampleMap.size());

    WriteCompanionFiles(sampleMap, audioTable);
    return true;
}

} // namespace OoT

#endif
