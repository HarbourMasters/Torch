#include "ScriptFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#include "storm/SWrapper.h"

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

    switch (opcode) {
        case 0:
        case 1: {
            auto f1 = (arg1 >> 8) & 1;
            auto f2 = (arg1 >> 7) & 1;
            auto f3 = arg1 & 0x7F;
            cmd << "EVENT_SET_" << (opcode ? "ACCEL" : "SPEED") << "(" << std::dec << f3 << ", " << s2 << ", " << (f1 ? "true" : "false") << ", " << (f2 ? "true" : "false")  << ")";
        } break;
        case 24:
            cmd << "EVENT_SET_ROTATE()";
            break;
        case 25:
            cmd << "EVENT_STOP_ROTATE()";
            break;
        case 96: 
            if(s2 >= 100) {
                cmd << "EVENT_BRANCH(EVC_100 + " << std::dec << (s2 - 100) << ", " << std::dec <<  arg1 << ")";
            } else {
                auto condition = Companion::Instance->GetEnumFromValue("EventCondition", s2).value_or("/* EVC_UNK */ " + std::to_string(s2));
                cmd << "EVENT_BRANCH(" << condition << ", " << std::dec <<  arg1 << ")";
            }
            break;
        case 104:
            cmd << "EVENT_INIT_ACTOR(" << std::dec << arg1 << ", " << s2 << ")";
            break;
        case 120: {
            auto rcidName = Companion::Instance->GetEnumFromValue("RadioCharacterId", arg1).value_or("/* RCID_UNK */ " + std::to_string(arg1));
            cmd << "EVENT_PLAY_MSG(" << rcidName << ", " << std::dec << std::setw(5) << s2 << ")";
        } break;
        case 124: {
            auto color = Companion::Instance->GetEnumFromValue("TexLineColor", s2).value_or("/* TXLC_UNK */ " + std::to_string(s2));
            cmd << "EVENT_MAKE_TEXLINE(" << color << ")";
        } break;
        case 125:
            cmd << "EVENT_STOP_TEXLINE()";
            break;
        case 126:
            cmd << "EVENT_LOOP(" << std::dec << arg1 << ", " << s2 << ")";
            break;
        case 127:
            cmd << "EVENT_STOP_SCRIPT()";
            break;
        default:
            cmd << "EVENT_CMD(" << enumName << ", " << std::dec << arg1 << ", " << s2 << ")";
            break;
    }
    

    return cmd.str();
}

ExportResult SF64::ScriptCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto script = std::static_pointer_cast<SF64::ScriptData>(raw);
    auto sortedPtrs = script->mPtrs;
    std::sort(sortedPtrs.begin(), sortedPtrs.end());
    std::vector<std::string> scriptNames;
    auto cmdOff = ASSET_PTR(script->mCmdsStart);
    auto cmdIndex = 0;

    for(int i = 0; i < sortedPtrs.size(); i++) {
        std::ostringstream scriptDefaultName;
        scriptDefaultName << symbol << "_cmds_" << std::uppercase << std::hex << cmdOff;

        if (Companion::Instance->IsDebug()) {
            write << "// 0x" << std::hex << std::uppercase << cmdOff << "\n";
        }
        write << "u16 " << scriptDefaultName.str() << "[] = {";

        auto cmdCount = script->mSizeMap[sortedPtrs[i]] / 2;
        for(int j = 0; j < cmdCount; j++, cmdIndex+=2) {
            // if((j % 3) == 0) {
            write << "\n" << fourSpaceTab << "/* " << std::setfill(' ') << std::setw(2) << std::dec << j << " */ ";
            // }
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

    write << "// 0x" << std::hex << std::uppercase << ASSET_PTR(offset) << "\n";
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
        static_cast<uint32_t>(offset + script->mPtrs.size() * sizeof(uint32_t))
    };
}

ExportResult SF64::ScriptBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto scriptWriter = LUS::BinaryWriter();
    auto script = std::static_pointer_cast<SF64::ScriptData>(raw);
    auto sortedPtrs = script->mPtrs;
    std::sort(sortedPtrs.begin(), sortedPtrs.end());
    uint32_t ptrCount = 0;
    uint32_t cmdIndex = 0;
    std::unordered_map<uint32_t, uint64_t> ptrMap;

    // Export Commands and Populate Hashes Map
    for (auto ptr : sortedPtrs) {
        auto wrapper = Companion::Instance->GetCurrentWrapper();
        std::ostringstream stream;
        stream.str("");
        stream.clear();

        auto cmdWriter = LUS::BinaryWriter();
        WriteHeader(cmdWriter, LUS::ResourceType::ScriptCmd, 0);

        auto cmdCount = script->mSizeMap.at(ptr) / 2;
        cmdWriter.Write((uint32_t) cmdCount);

        // Writing in pairs for readability
        for (uint32_t i = 0; i < cmdCount; ++i) {
            cmdWriter.Write(script->mCmds.at(cmdIndex++));
            cmdWriter.Write(script->mCmds.at(cmdIndex++));
        }

        cmdWriter.Finish(stream);

        auto data = stream.str();
        wrapper->CreateFile(entryName + "_cmd_" + std::to_string(ptrCount), std::vector(data.begin(), data.end()));

        std::ostringstream cmdName;
        cmdName << entryName << "_cmd_" << std::dec << ptrCount;
        ptrMap[ptr] = CRC64(cmdName.str().c_str());
        ptrCount++;
    }

    // Export Script
    WriteHeader(scriptWriter, LUS::ResourceType::Script, 0);
    scriptWriter.Write((uint32_t) script->mPtrs.size());
    for (auto &ptr : script->mPtrs) {
        scriptWriter.Write(ptrMap.at(ptr));
    }

    scriptWriter.Finish(write);

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
