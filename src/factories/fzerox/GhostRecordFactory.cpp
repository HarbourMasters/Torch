#include "GhostRecordFactory.h"
#include "ghost/Ghost.h"

#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

#define ARRAY_COUNT(arr) (int32_t)(sizeof(arr) / sizeof(arr[0]))
#define ALIGN16(val) (((val) + 0xF) & ~0xF)

uint16_t FZX::GhostRecordData::Save_CalculateChecksum(void* data, int32_t size) {
    uint8_t* dataPtr = (uint8_t*)data;
    uint16_t checksum = 0;
    int32_t i;

    for (i = 0; i < size; i++) {
        checksum += *dataPtr++;
    }

    return checksum;
}

uint16_t FZX::GhostRecordData::CalculateRecordChecksum(void) {
    uint16_t checksum = 0;
    int32_t recordChecksum = CalculateReplayChecksum();

    checksum += Save_CalculateChecksum(&mGhostType, sizeof(mGhostType));
    checksum += Save_CalculateChecksum(&recordChecksum, sizeof(recordChecksum));
    checksum += Save_CalculateChecksum(&mCourseEncoding, sizeof(mCourseEncoding));
    checksum += Save_CalculateChecksum(&mRaceTime, sizeof(mRaceTime));
    checksum += Save_CalculateChecksum(&mUnk10, sizeof(mUnk10));
    checksum += Save_CalculateChecksum((void*)mTrackName.c_str(), mTrackName.length());
    checksum += Save_CalculateChecksum(&mGhostMachineInfo, sizeof(mGhostMachineInfo));
    
    return checksum;
}

uint16_t FZX::GhostRecordData::CalculateDataChecksum(void) {
    uint16_t checksum = 0;
    
    checksum += Save_CalculateChecksum(&mReplayEnd, sizeof(mReplayEnd));
    checksum += Save_CalculateChecksum(&mReplaySize, sizeof(mReplaySize));

    for (auto lapTime : mLapTimes) {
        checksum += Save_CalculateChecksum(&lapTime, sizeof(lapTime));
    }

    checksum += Save_CalculateChecksum(mReplayData.data(), mReplayData.size());

    return checksum;
}

int32_t FZX::GhostRecordData::CalculateReplayChecksum(void) {
    int32_t checksum = 0;
    int32_t i = 0;

    for (auto dataByte : mReplayData) {
        checksum += ((uint32_t)(uint8_t)dataByte) << ((3 - i) * 8);

        i++;
        i %= 4;
    }

    return checksum;
}

ExportResult FZX::GhostRecordHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern GhostRecord " << symbol << "Record;\n";
    write << "extern GhostReplayInfo " << symbol << "ReplayInfo;\n";
    write << "extern s8 " << symbol << "Data[];\n";

    return std::nullopt;
}

ExportResult FZX::GhostRecordCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto record = std::static_pointer_cast<GhostRecordData>(raw);

    write << "GhostRecord " << symbol << "Record = {\n";

    write << fourSpaceTab << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << record->CalculateRecordChecksum() << std::dec << ", /* Checksum */\n";

    write << fourSpaceTab << static_cast<GhostType>(record->mGhostType) << ",\n";

    write << fourSpaceTab << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << record->CalculateReplayChecksum() << std::dec << ", /* Replay Checksum */\n";

    write << fourSpaceTab << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << record->mCourseEncoding << std::dec << ", /* Course Encoding */\n";

    write << fourSpaceTab << record->mRaceTime << ", /* Race Time */ \n";

    write << fourSpaceTab << record->mUnk10 << ", { 0 },\n";

    write << fourSpaceTab << "\"" << record->mTrackName << "\",\n";

    write << fourSpaceTab << " {\n";
    write << fourSpaceTab << fourSpaceTab << static_cast<Character>(record->mGhostMachineInfo.character) << ", ";
    write << static_cast<CustomType>(record->mGhostMachineInfo.customType) << ",\n";

    write << fourSpaceTab << fourSpaceTab << static_cast<FrontType>(record->mGhostMachineInfo.frontType) << ", ";
    write << static_cast<RearType>(record->mGhostMachineInfo.rearType) << ", ";
    write << static_cast<WingType>(record->mGhostMachineInfo.wingType) << ",\n";

    write << fourSpaceTab << fourSpaceTab << static_cast<Logo>(record->mGhostMachineInfo.frontType) << ", ";
    write << "MACHINE_NUMBER(" << (uint32_t)record->mGhostMachineInfo.number + 1 << "), ";
    write << static_cast<Decal>(record->mGhostMachineInfo.decal) << ",\n";

    write << fourSpaceTab << fourSpaceTab << (uint32_t)record->mGhostMachineInfo.bodyR << ", ";
    write << (uint32_t)record->mGhostMachineInfo.bodyG << ", ";
    write << (uint32_t)record->mGhostMachineInfo.bodyB << ", /* Body Color */\n";
    write << fourSpaceTab << fourSpaceTab << (uint32_t)record->mGhostMachineInfo.numberR << ", ";
    write << (uint32_t)record->mGhostMachineInfo.numberG << ", ";
    write << (uint32_t)record->mGhostMachineInfo.numberB << ", /* Number Color */\n";
    write << fourSpaceTab << fourSpaceTab << (uint32_t)record->mGhostMachineInfo.decalR << ", ";
    write << (uint32_t)record->mGhostMachineInfo.decalG << ", ";
    write << (uint32_t)record->mGhostMachineInfo.decalB << ", /* Decal Color */\n";
    write << fourSpaceTab << fourSpaceTab << (uint32_t)record->mGhostMachineInfo.cockpitR << ", ";
    write << (uint32_t)record->mGhostMachineInfo.cockpitG << ", ";
    write << (uint32_t)record->mGhostMachineInfo.cockpitB << "  /* Cockpit Color */\n";
    write << fourSpaceTab << "}\n";

    write << "};\n\n";


    write << "GhostReplayInfo " << symbol << "ReplayInfo = {\n";

    write << fourSpaceTab << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << record->CalculateDataChecksum() << std::dec << ", /* Checksum */\n";

    write << fourSpaceTab << "0,\n";

    write << fourSpaceTab << "{ ";

    for (int32_t i = 0; i < 3; i++) {
        write << record->mLapTimes.at(i);
        if (i != 2) {
            write << ", ";
        }
    }

    write << " },\n";

    write << fourSpaceTab << record->mReplayEnd << ", ";

    write << record->mReplaySize << ",\n";

    write << fourSpaceTab << "0, 0\n";

    write << "};\n\n";


    write << "u8 " << symbol << "Data[] = {\n";

    write << fourSpaceTab;

    int32_t counter = 0;
    for (uint32_t i = 0; i < record->mReplaySize; i++) {
        int32_t replayData = (int32_t)record->mReplayData.at(i);

        if (replayData == -0x80) {
            write << "";

            replayData = ((int32_t)record->mReplayData.at(i + 1) << 8) | (int32_t)record->mReplayData.at(i + 2);
            write << "REPLAY_DATA_LARGE(" << replayData << ")";

            i += 2;
        } else {
            write << replayData;
        }

        if (i != record->mReplaySize - 1) {
            write << ", ";
        } else {
            write << "\n";
            break;
        }

        if ((++counter % 3) == 0) {
            write << "\n" << fourSpaceTab;
        }
    }

    write << "};\n\n";

    return offset + 0x20 + 0x40 + ALIGN16(record->mReplaySize);
}

ExportResult FZX::GhostRecordBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto record = std::static_pointer_cast<GhostRecordData>(raw);

    WriteHeader(writer, Torch::ResourceType::GhostRecord, 0);

    writer.Finish(write);
    return std::nullopt;
}

ExportResult FZX::GhostRecordModdingExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto record = std::static_pointer_cast<GhostRecordData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    std::stringstream stream;
    YAML::Emitter out;

    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;

    out << YAML::EndMap;

    write.write(out.c_str(), out.size());

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> FZX::GhostRecordFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);

    reader.SetEndianness(Torch::Endianness::Big);

    // Record
    uint16_t recordChecksum = reader.ReadInt16();
    uint16_t ghostType = reader.ReadUInt16();
    int32_t replayChecksum = reader.ReadInt32();
    int32_t courseEncoding = reader.ReadInt32();
    int32_t raceTime = reader.ReadInt32();
    
    uint16_t unk_10 = reader.ReadInt16();
    for (int32_t i = 0; i < 5; i++) {
        reader.ReadInt8();
    }
    
    char trackNameBuffer[9];
    for (int32_t i = 0; i < ARRAY_COUNT(trackNameBuffer); i++) {
        trackNameBuffer[i] = reader.ReadChar();
    }
    std::string trackName(trackNameBuffer);

    GhostMachineInfo ghostMachineInfo;

    ghostMachineInfo.character = reader.ReadUByte();
    ghostMachineInfo.customType = reader.ReadUByte();
    ghostMachineInfo.frontType = reader.ReadUByte();
    ghostMachineInfo.rearType = reader.ReadUByte();
    ghostMachineInfo.wingType = reader.ReadUByte();
    ghostMachineInfo.logo = reader.ReadUByte();
    ghostMachineInfo.number = reader.ReadUByte();
    ghostMachineInfo.decal = reader.ReadUByte();
    ghostMachineInfo.bodyR = reader.ReadUByte();
    ghostMachineInfo.bodyG = reader.ReadUByte();
    ghostMachineInfo.bodyB = reader.ReadUByte();
    ghostMachineInfo.numberR = reader.ReadUByte();
    ghostMachineInfo.numberG = reader.ReadUByte();
    ghostMachineInfo.numberB = reader.ReadUByte();
    ghostMachineInfo.decalR = reader.ReadUByte();
    ghostMachineInfo.decalG = reader.ReadUByte();
    ghostMachineInfo.decalB = reader.ReadUByte();
    ghostMachineInfo.cockpitR = reader.ReadUByte();
    ghostMachineInfo.cockpitG = reader.ReadUByte();
    ghostMachineInfo.cockpitB = reader.ReadUByte();

    // Data
    reader.Seek(0x40, LUS::SeekOffsetType::Start);

    uint16_t dataChecksum = reader.ReadInt16();

    reader.ReadInt16();

    std::vector<int32_t> lapTimes;

    for (int32_t i = 0; i < 3; i++) {
        lapTimes.push_back(reader.ReadInt32());
    }

    int32_t replayEnd = reader.ReadInt32();
    uint32_t replaySize = reader.ReadUInt32();

    reader.ReadInt32();
    reader.ReadInt32();

    std::vector<int8_t> replayData;
    for (uint32_t i = 0; i < replaySize; i++) {
        replayData.push_back(reader.ReadInt8());
    }

    return std::make_shared<GhostRecordData>(ghostType, courseEncoding, raceTime, unk_10, trackName, ghostMachineInfo, lapTimes, replayEnd, replaySize, replayData);
}

/*
std::optional<std::shared_ptr<IParsedData>> FZX::GhostRecordFactory::parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) {
    YAML::Node assetNode;
    
    try {
        std::string text((char*) buffer.data(), buffer.size());
        assetNode = YAML::Load(text.c_str());
    } catch (YAML::ParserException& e) {
        SPDLOG_ERROR("Failed to parse message data: {}", e.what());
        SPDLOG_ERROR("{}", (char*) buffer.data());
        return std::nullopt;
    }
    
    const auto info = assetNode.begin()->second;
    
    return std::make_shared<GhostRecordData>();
}
*/
