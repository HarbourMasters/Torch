#ifdef OOT_SUPPORT

#include "OoTAudioSequenceWriter.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <iomanip>
#include <sstream>

namespace OoT {

void AudioSequenceWriter::WriteCompanion(const uint8_t* seqData, uint32_t seqSize,
                                         uint32_t originalIndex, uint8_t medium, uint8_t cachePolicy,
                                         const std::vector<uint8_t>& fonts, const std::string& seqName) {
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTAudioSequence, 2);

    w.Write(seqSize);
    w.Write((char*)seqData, seqSize);

    w.Write(static_cast<uint8_t>(originalIndex));
    w.Write(medium);
    w.Write(cachePolicy);

    w.Write(static_cast<uint32_t>(fonts.size()));
    for (auto f : fonts) {
        w.Write(f);
    }

    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    Companion::Instance->RegisterCompanionFile(
        "sequences/" + seqName, std::vector<char>(str.begin(), str.end()));
}

bool AudioSequenceWriter::Extract(std::vector<uint8_t>& buffer, YAML::Node& node,
                                  const std::vector<AudioTableEntry>& seqTable,
                                  const std::vector<std::vector<uint8_t>>& seqFontMap) {
    // Get Audioseq ROM data (segment 2, uncompressed)
    auto audioseqSeg = Companion::Instance->GetFileOffsetFromSegmentedAddr(2);
    if (!audioseqSeg.has_value()) {
        SPDLOG_ERROR("OoTAudioFactory: Audioseq segment not found");
        return false;
    }
    uint32_t audioseqOff = audioseqSeg.value();

    // Get sequence names from YAML
    std::vector<std::string> seqNames;
    if (node["sequences"] && node["sequences"].IsSequence()) {
        auto seqNode = node["sequences"];
        for (size_t i = 0; i < seqNode.size(); i++) {
            seqNames.push_back(seqNode[i].as<std::string>());
        }
    }

    for (uint32_t i = 0; i < seqTable.size(); i++) {
        auto medium = seqTable[i].medium;
        auto cachePolicy = seqTable[i].cachePolicy;
        auto& fonts = seqFontMap[i];

        // Resolve alias: size==0 means ptr holds the index of the real sequence
        bool isAlias = (seqTable[i].size == 0);
        uint32_t dataIdx = isAlias ? seqTable[i].ptr : i;
        if (dataIdx >= seqTable.size()) {
            SPDLOG_WARN("OoTAudioFactory: sequence {} alias index {} out of range", i, dataIdx);
            continue;
        }
        auto& dataEntry = seqTable[dataIdx];

        // Sequence name
        std::string seqName;
        if (i < seqNames.size()) {
            seqName = seqNames[i];
        } else {
            std::ostringstream ss;
            ss << std::setfill('0') << std::setw(3) << i << "_Sequence";
            seqName = ss.str();
        }

        // Resolve sequence data location in ROM (using dataEntry for aliased sequences)
        uint32_t seqDataOff = audioseqOff + dataEntry.ptr;
        if (seqDataOff + dataEntry.size > buffer.size()) {
            SPDLOG_WARN("OoTAudioFactory: sequence {} out of bounds", seqName);
            continue;
        }

        WriteCompanion(buffer.data() + seqDataOff, dataEntry.size,
                        i, medium, cachePolicy, fonts, seqName);
    }

    SPDLOG_INFO("OoTAudioFactory: wrote {} sequence companion files", seqTable.size());
    return true;
}

} // namespace OoT

#endif
