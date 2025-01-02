#include "CourseFactory.h"

#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <types/Vec3D.h>

#define ARRAY_COUNT(arr) (int32_t)(sizeof(arr) / sizeof(arr[0]))

#define TRACK_SEGMENT_JOINT_MASK 0x600
#define TRACK_SEGMENT_FORM_MASK 0x38000
#define TRACK_SEGMENT_FLAG_CONTINUE 0x40000000

#define FORMAT_HEX(x, w) std::hex << std::setfill('0') << std::setw(w) << x
#define FORMAT_FLOAT(x, w, p) std::dec << std::setfill(' ') << std::fixed << std::setprecision(p) << std::setw(w) << x

uint32_t FZX::CourseData::CalculateChecksum(void) {
    uint32_t checksum = mControlPointInfos.size();
    int32_t counter = 0;

    for (const auto& controlPointInfo : mControlPointInfos) {
        int32_t trackSegmentInfo = controlPointInfo.controlPoint.trackSegmentInfo;
        trackSegmentInfo &= ~TRACK_SEGMENT_JOINT_MASK;
        trackSegmentInfo &= ~TRACK_SEGMENT_FORM_MASK;
        trackSegmentInfo &= ~TRACK_SEGMENT_FLAG_CONTINUE;

        checksum += (int32_t) ((controlPointInfo.controlPoint.pos.x + ((1.1f + (0.7f * counter)) * controlPointInfo.controlPoint.pos.y)) +
                   ((2.2f + (1.2f * counter)) * controlPointInfo.controlPoint.pos.z * (4.4f + (0.9f * counter))) +
                   controlPointInfo.controlPoint.radiusLeft + ((5.5f + (0.8f * counter)) * controlPointInfo.controlPoint.radiusRight * 4.8f)) +
            trackSegmentInfo * (0xFE - counter) + controlPointInfo.bankAngle * (0x93DE - counter * 2);
        checksum += (controlPointInfo.pit * counter);
        checksum += (controlPointInfo.boost * (counter + 0x10));
        checksum += (controlPointInfo.dirt * (counter + 0x80));
        checksum += (controlPointInfo.ice * (counter + 0x100));
        checksum += (controlPointInfo.jump * (counter + 0x800));
        checksum += (controlPointInfo.landmine * (counter + 0x1000));
        checksum += (controlPointInfo.gate * (counter + 0x8000));
        checksum += (controlPointInfo.building * (counter + 0x10000));
        checksum += (controlPointInfo.sign * (counter + 0x80000));
        counter++;
    }


    return checksum;
}

ExportResult FZX::CourseHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern CourseData " << symbol << ";\n";

    return std::nullopt;
}

ExportResult FZX::CourseCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto course = std::static_pointer_cast<CourseData>(raw);
    size_t controlPointCount = course->mControlPointInfos.size();

    write << "CourseData " << symbol << " = {\n";

    write << std::dec;
    write << fourSpaceTab << (int32_t)course->mCreatorId << ", /* Creator Id */\n";
    write << fourSpaceTab << (int32_t)controlPointCount << ", /* Control Point Count */\n";
    write << fourSpaceTab << (int32_t)course->mVenue << ", /* Venue */\n";
    write << fourSpaceTab << (int32_t)course->mSkybox << ", /* Skybox */\n";
    write << fourSpaceTab << "0x" << std::hex << std::uppercase << course->CalculateChecksum() << std::dec << ", /* Checksum */\n";
    write << fourSpaceTab << (int32_t)course->mFlag << ", /* Flag */\n";

    write << fourSpaceTab << "\"" << course->mFileName << std::hex;
    for (size_t i = 0; i < 22 - course->mFileName.length(); i++) {
        write << "\\x" << FORMAT_HEX((uint32_t)(uint8_t)course->mFileNameExtra.at(i), 2);
    }
    write << std::dec << "\", /* File Name */\n";

    write << fourSpaceTab << "{ /* Control Points */\n";

    for (size_t i = 0; i < 64; i++) {
        write << fourSpaceTab << fourSpaceTab;
        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            Vec3f pos(controlPointInfo.controlPoint.pos.x, controlPointInfo.controlPoint.pos.y, controlPointInfo.controlPoint.pos.z);
            write << "{ { " << FORMAT_FLOAT(pos, 6, 4) << "}, ";
            write << controlPointInfo.controlPoint.radiusLeft << ", ";
            write << controlPointInfo.controlPoint.radiusRight << ", ";
            write << "0x" << std::hex << controlPointInfo.controlPoint.trackSegmentInfo << std::dec;
            write << " },\n";

        } else {
            write << "{ 0 },\n";
        }
    }
    write << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Bank Angle */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << controlPointInfo.bankAngle << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Pit */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.pit << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Boost */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.boost << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Dirt */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.dirt << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Ice */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.ice << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Jump */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.jump << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Landmine */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.landmine << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Gate */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.gate << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Building */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.building << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "},\n";

    write << fourSpaceTab << "{ /* Sign */";
    for (size_t i = 0; i < 64; i++) {
        if ((i % 16) == 0) {
            write << "\n" << fourSpaceTab << fourSpaceTab;
        }

        if (i < controlPointCount) {
            const ControlPointInfo& controlPointInfo = course->mControlPointInfos.at(i);
            write << (int32_t)controlPointInfo.sign << ", ";
        } else {
            write << "0, ";
        }
    }
    write << "\n" << fourSpaceTab << "}\n";
    write << "};\n\n";

    return offset + sizeof(CourseRawData);
}

ExportResult FZX::CourseBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto course = std::static_pointer_cast<CourseData>(raw);
    int8_t controlPointCount = (int8_t)course->mControlPointInfos.size();

    WriteHeader(writer, Torch::ResourceType::CourseData, 0);

    writer.Write(course->mCreatorId);
    writer.Write(controlPointCount);
    writer.Write(course->mVenue);
    writer.Write(course->mSkybox);
    writer.Write(course->CalculateChecksum());
    writer.Write(course->mFlag);
    writer.Write(course->mFileName);
    for (const auto extra : course->mFileNameExtra) {
        writer.Write(extra);
    }
    for (const auto& controlPointInfo : course->mControlPointInfos) {
        writer.Write(controlPointInfo.controlPoint.pos.x);
        writer.Write(controlPointInfo.controlPoint.pos.y);
        writer.Write(controlPointInfo.controlPoint.pos.z);
        writer.Write(controlPointInfo.controlPoint.radiusLeft);
        writer.Write(controlPointInfo.controlPoint.radiusRight);
        writer.Write(controlPointInfo.controlPoint.trackSegmentInfo);
        writer.Write(controlPointInfo.bankAngle);
        writer.Write(controlPointInfo.pit);
        writer.Write(controlPointInfo.boost);
        writer.Write(controlPointInfo.dirt);
        writer.Write(controlPointInfo.ice);
        writer.Write(controlPointInfo.jump);
        writer.Write(controlPointInfo.landmine);
        writer.Write(controlPointInfo.gate);
        writer.Write(controlPointInfo.building);
        writer.Write(controlPointInfo.sign);
    }

    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> FZX::CourseFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);

    reader.SetEndianness(Torch::Endianness::Big);

    int8_t creatorId = reader.ReadInt8();
    int8_t controlPointCount = reader.ReadInt8();
    int8_t venue = reader.ReadInt8();
    int8_t skybox = reader.ReadInt8();
    uint32_t checksum = reader.ReadUInt32();
    int8_t flag = reader.ReadInt8();
    char nameBuffer[23];

    for (int32_t i = 0; i < ARRAY_COUNT(nameBuffer); i++) {
        nameBuffer[i] = reader.ReadInt8();
    }

    std::string fileName(nameBuffer);
    std::vector<int8_t> fileNameExtra;
    for (int32_t i = fileName.length(); i < ARRAY_COUNT(nameBuffer); i++) {
        fileNameExtra.push_back(nameBuffer[i]);
    }

    std::vector<ControlPointInfo> controlPointInfos;

    for (int8_t i = 0; i < controlPointCount; i++) {
        ControlPointInfo controlPointInfo;

        reader.Seek(0x020 + i * sizeof(ControlPoint), LUS::SeekOffsetType::Start);
        controlPointInfo.controlPoint.pos.x = reader.ReadFloat();
        controlPointInfo.controlPoint.pos.y = reader.ReadFloat();
        controlPointInfo.controlPoint.pos.z = reader.ReadFloat();
        controlPointInfo.controlPoint.radiusLeft = reader.ReadInt16();
        controlPointInfo.controlPoint.radiusRight = reader.ReadInt16();
        controlPointInfo.controlPoint.trackSegmentInfo = reader.ReadInt32();
        reader.Seek(0x520 + i * sizeof(int16_t), LUS::SeekOffsetType::Start);
        controlPointInfo.bankAngle = reader.ReadInt16();
        reader.Seek(0x5A0 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.pit = reader.ReadInt8();
        reader.Seek(0x5E0 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.boost = reader.ReadInt8();
        reader.Seek(0x620 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.dirt = reader.ReadInt8();
        reader.Seek(0x660 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.ice = reader.ReadInt8();
        reader.Seek(0x6A0 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.jump = reader.ReadInt8();
        reader.Seek(0x6E0 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.landmine = reader.ReadInt8();
        reader.Seek(0x720 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.gate = reader.ReadInt8();
        reader.Seek(0x760 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.building = reader.ReadInt8();
        reader.Seek(0x7A0 + i * sizeof(int8_t), LUS::SeekOffsetType::Start);
        controlPointInfo.sign = reader.ReadInt8();
        controlPointInfos.push_back(controlPointInfo);
    }

    return std::make_shared<CourseData>(creatorId, venue, skybox, flag, fileName, fileNameExtra, controlPointInfos);
}
