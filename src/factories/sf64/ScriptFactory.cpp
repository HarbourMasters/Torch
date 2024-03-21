#include "ScriptFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

SF64::ScriptData::ScriptData(std::vector<uint32_t> ptrs, std::vector<uint16_t> cmds, std::map<uint32_t, int> sizeMap, uint32_t ptrsStart, uint32_t cmdsStart): mPtrs(ptrs), mCmds(cmds), mSizeMap(sizeMap), mPtrsStart(ptrsStart), mCmdsStart(cmdsStart) {

}

ExportResult SF64::ScriptHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern u16* " << symbol << "[];\n";
    return std::nullopt;
}

std::string MakeScriptCmd(uint16_t s1, uint16_t s2) {
    auto opcode = (s1 & 0xFE00) >> 9;
    auto arg1 = s1 & 0x1FF;
    std::ostringstream cmd;

    auto enumName = Companion::Instance->GetEnumFromValue("EventOpcode", opcode).value_or("/* EVOP_UNK */ " + std::to_string(opcode));

    cmd << "EVENT_CMD(" << enumName << ", " << std::right << std::setw(3) << std::dec << arg1 << ", " << std::setw(5) << std::dec << s2 << ")";

    return cmd.str();
}

ExportResult SF64::ScriptCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto script = std::static_pointer_cast<SF64::ScriptData>(raw);
    auto sortedPtrs = script->mPtrs;
    std::sort(sortedPtrs.begin(), sortedPtrs.end());
    std::vector<std::string> scriptNames;
    auto cmdOff = script->mCmdsStart;
    auto cmdIndex = 0;

    if(IS_SEGMENTED(cmdOff)) {
        cmdOff = SEGMENT_OFFSET(cmdOff);
    }

    for(int i = 0; i < sortedPtrs.size(); i++) {
        std::ostringstream scriptDefaultName;
        scriptDefaultName << symbol << "_cmds_" << std::uppercase << std::hex << cmdOff;

        if (Companion::Instance->IsDebug()) {
            write << "// 0x" << std::hex << std::uppercase << cmdOff << "\n";
        }
        write << "u16 " << scriptDefaultName.str() << "[] = {";

        auto cmdCount = script->mSizeMap[sortedPtrs[i]] / 2;
        for(int j = 0; j < cmdCount; j++, cmdIndex+=2) {
            if((j % 3) == 0) {
                write << "\n" << fourSpaceTab;
            }
            write << MakeScriptCmd(script->mCmds[cmdIndex], script->mCmds[cmdIndex + 1])  << ", ";
        }
        scriptNames.push_back(scriptDefaultName.str());
        write << "\n};\n";

        cmdOff += 4 * cmdCount;
        if (Companion::Instance->IsDebug()) {
            write << "// count: " << cmdCount << " commands\n";
            write << "// 0x" << std::hex << std::uppercase << cmdOff << "\n";
        }

        write << "\n";
    }

    if (Companion::Instance->IsDebug()) {
        if (IS_SEGMENTED(offset)) {
            offset = SEGMENT_OFFSET(offset);
        }
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

    write << "u16* " << symbol << "[] = {";
    for(int i = 0; i < script->mPtrs.size(); i++) {
        if((i % 5) == 0) {
            write << "\n" << fourSpaceTab;
        }
        write << scriptNames[i] << ", ";
    }
    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// count: " << script->mPtrs.size() << " events\n";
    }

    return OffsetEntry {
        script->mCmdsStart,
        static_cast<uint32_t>((IS_SEGMENTED(offset) ? SEGMENT_OFFSET(offset) : offset) + script->mPtrs.size() * sizeof(uint32_t))
    };
}

ExportResult SF64::ScriptBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SF64::ScriptFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto ptrsStart = offset;
    YAML::Node scriptNode;
    std::vector<uint32_t> scriptPtrs;
    std::vector<uint16_t> scriptCmds;
    std::map<uint32_t, int> sizeMap;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x1000);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);
    auto ptr = reader.ReadUInt32();

    while(SEGMENT_NUMBER(ptr) == SEGMENT_NUMBER(offset)) {
        scriptPtrs.push_back(ptr);
        ptr = reader.ReadUInt32();
    }

    auto sortedPtrs = scriptPtrs;
    std::sort(sortedPtrs.begin(), sortedPtrs.end());

    auto cmdsStart = sortedPtrs[0];

    for(int i = 0; i < sortedPtrs.size() - 1; i++) {
        sizeMap[sortedPtrs[i]] = (sortedPtrs[i+1] - sortedPtrs[i]) / sizeof(uint16_t);
    }
    sizeMap[sortedPtrs[sortedPtrs.size() - 1]] = (ptrsStart - sortedPtrs[sortedPtrs.size() - 1]) / sizeof(uint16_t);

    auto scriptLen = (ptrsStart - cmdsStart) / sizeof(uint16_t);

    scriptNode["offset"] = cmdsStart;
    auto [__, scriptSegment] = Decompressor::AutoDecode(scriptNode, buffer, scriptLen * sizeof(uint16_t));
    LUS::BinaryReader scriptReader(scriptSegment.data, scriptSegment.size);
    scriptReader.SetEndianness(LUS::Endianness::Big);
    for(int i = 0; i < scriptLen; i++) {
        scriptCmds.push_back(scriptReader.ReadUInt16());
    }
    return std::make_shared<SF64::ScriptData>(scriptPtrs, scriptCmds, sizeMap, ptrsStart, cmdsStart);
}
