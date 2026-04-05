#ifdef OOT_SUPPORT

#include "OoTCutsceneFactory.h"
#include "OoTSceneUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

#include <set>

namespace OoT {

// Cutscene serialization — shared with OoTSceneFactory's SetCutscenes handler.
std::vector<char> SerializeCutscene(std::vector<uint8_t>& buffer, uint32_t segAddr) {
    auto csSizeCalc = ReadSubArray(buffer, segAddr, 0x10000);
    uint32_t csMaxBytes = csSizeCalc.GetLength();
    if (csMaxBytes < 8) return {};

    uint32_t numCsCommands = csSizeCalc.ReadUInt32();
    csSizeCalc.ReadUInt32();

    bool csParseOk = true;
    for (uint32_t csCmd = 0; csCmd < numCsCommands && csParseOk; csCmd++) {
        if (csSizeCalc.GetBaseAddress() + 8 > csMaxBytes) { csParseOk = false; break; }
        uint32_t csCmdId = csSizeCalc.ReadUInt32();
        if (csCmdId == 0xFFFFFFFF) break;
        uint32_t csCmdWord2 = csSizeCalc.ReadUInt32();
        if (csCmdId == 1 || csCmdId == 2 || csCmdId == 5 || csCmdId == 6) {
            if (csSizeCalc.GetBaseAddress() + 4 > csMaxBytes) { csParseOk = false; break; }
            csSizeCalc.ReadUInt32();
            for (uint32_t ci = 0; ci < 1000; ci++) {
                if (csSizeCalc.GetBaseAddress() + 0x10 > csMaxBytes) { csParseOk = false; break; }
                uint8_t cf = csSizeCalc.ReadUByte();
                csSizeCalc.Seek(csSizeCalc.GetBaseAddress() + 0x0F, LUS::SeekOffsetType::Start);
                if (cf == 0xFF) break;
            }
        } else if (csCmdId == 0x2D || csCmdId == 0x3E8) {
            if (csSizeCalc.GetBaseAddress() + 0x08 > csMaxBytes) { csParseOk = false; break; }
            csSizeCalc.Seek(csSizeCalc.GetBaseAddress() + 0x08, LUS::SeekOffsetType::Start);
        } else {
            uint32_t entrySize = (csCmdId == 0x09 || csCmdId == 0x13 || csCmdId == 0x8C) ? 0x0C : 0x30;
            uint32_t skipBytes = csCmdWord2 * entrySize;
            if (csSizeCalc.GetBaseAddress() + skipBytes > csMaxBytes) { csParseOk = false; break; }
            csSizeCalc.Seek(csSizeCalc.GetBaseAddress() + skipBytes, LUS::SeekOffsetType::Start);
        }
    }
    if (!csParseOk || csSizeCalc.GetBaseAddress() + 8 > csMaxBytes) return {};
    csSizeCalc.ReadUInt32(); csSizeCalc.ReadUInt32();
    uint32_t totalCsBytes = csSizeCalc.GetBaseAddress();

    // Re-serialize
    auto csReader = ReadSubArray(buffer, segAddr, totalCsBytes);
    totalCsBytes = csReader.GetLength();
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::OoTCutscene, 0);
    uint32_t sizePos = w.GetStream()->GetLength();
    w.Write(static_cast<uint32_t>(0));
    uint32_t startPos = w.GetStream()->GetLength();

    uint32_t csNumCmds = csReader.ReadUInt32();
    uint32_t csEndFrame = csReader.ReadUInt32();
    w.Write(csNumCmds); w.Write(csEndFrame);

    static const std::set<uint32_t> unimplemented = {
        0x0B,0x0C,0x0D,0x14,0x15,0x16,0x1A,0x1B,0x1C,0x20,0x21,0x38,0x3B,0x3D,
        0x47,0x49,0x5B,0x5C,0x5F,0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,
        0x6D,0x70,0x71,0x7A
    };

    for (uint32_t ci = 0; ci < csNumCmds && csReader.GetBaseAddress() + 8 <= totalCsBytes; ci++) {
        uint32_t cid = csReader.ReadUInt32();
        if (cid == 0xFFFFFFFF) break;

        bool isHandled = (cid >= 0x01 && cid <= 0x0A) || cid == 0x03 || cid == 0x04 ||
                         cid == 0x13 || cid == 0x2D || cid == 0x56 || cid == 0x57 ||
                         cid == 0x7C || cid == 0x8C || cid == 0x3E8;
        if (!isHandled && cid >= 0x0E && cid <= 0x90)
            isHandled = !unimplemented.count(cid);

        if (!isHandled) {
            if (cid == 0x07 || cid == 0x08) {
                csReader.ReadUInt32(); csReader.ReadUInt32();
                while (csReader.GetBaseAddress() + 0x10 <= totalCsBytes) {
                    uint8_t cf = csReader.ReadUByte();
                    csReader.Seek(csReader.GetBaseAddress() + 0x0F, LUS::SeekOffsetType::Start);
                    if (cf == 0xFF) break;
                }
            } else {
                uint32_t sc = csReader.ReadUInt32();
                uint32_t ss = sc * 0x30;
                if (csReader.GetBaseAddress() + ss <= totalCsBytes)
                    csReader.Seek(csReader.GetBaseAddress() + ss, LUS::SeekOffsetType::Start);
            }
            continue;
        }

        w.Write(cid);
        if (cid == 1 || cid == 2 || cid == 5 || cid == 6) {
            uint16_t a=csReader.ReadUInt16(),b=csReader.ReadUInt16();
            w.Write(CS_CMD_HH(a,b));
            uint16_t c=csReader.ReadUInt16(),d=csReader.ReadUInt16();
            w.Write(CS_CMD_HH(c,d));
            while (true) {
                int8_t cf=csReader.ReadInt8(),cr=csReader.ReadInt8();
                int16_t npf=csReader.ReadInt16(); float va=csReader.ReadFloat();
                int16_t px=csReader.ReadInt16(),py=csReader.ReadInt16();
                int16_t pz=csReader.ReadInt16(),pu=csReader.ReadInt16();
                w.Write(CS_CMD_BBH(cf,cr,npf)); w.Write(va);
                w.Write(CS_CMD_HH(px,py)); w.Write(CS_CMD_HH(pz,pu));
                if ((uint8_t)cf == 0xFF) break;
            }
        } else if (cid == 0x2D || cid == 0x3E8) {
            csReader.ReadUInt32(); w.Write(static_cast<uint32_t>(1));
            uint16_t a=csReader.ReadUInt16(),b=csReader.ReadUInt16();
            uint16_t c=csReader.ReadUInt16(); csReader.ReadUInt16();
            w.Write(CS_CMD_HH(a,b)); w.Write(CS_CMD_HH(c,c));
        } else {
            uint32_t ec = csReader.ReadUInt32(); w.Write(ec);
            for (uint32_t ei = 0; ei < ec; ei++) {
                if (cid == 0x09) {
                    uint16_t base=csReader.ReadUInt16(),sf=csReader.ReadUInt16(),ef=csReader.ReadUInt16();
                    uint8_t ss2=csReader.ReadUByte(),dur=csReader.ReadUByte();
                    uint8_t dr=csReader.ReadUByte(),u9=csReader.ReadUByte(); uint16_t ua=csReader.ReadUInt16();
                    w.Write(CS_CMD_HH(base,sf)); w.Write(CS_CMD_HBB(ef,ss2,dur)); w.Write(CS_CMD_BBH(dr,u9,ua));
                } else if (cid == 0x13) {
                    uint16_t base=csReader.ReadUInt16(),sf=csReader.ReadUInt16();
                    uint16_t ef=csReader.ReadUInt16(),ty=csReader.ReadUInt16();
                    uint16_t t1=csReader.ReadUInt16(),t2=csReader.ReadUInt16();
                    w.Write(CS_CMD_HH(base,sf)); w.Write(CS_CMD_HH(ef,ty)); w.Write(CS_CMD_HH(t1,t2));
                } else if (cid == 0x8C) {
                    uint16_t base=csReader.ReadUInt16(),sf=csReader.ReadUInt16(),ef=csReader.ReadUInt16();
                    uint8_t hr=csReader.ReadUByte(),mn=csReader.ReadUByte(); csReader.ReadUInt32();
                    w.Write(CS_CMD_HH(base,sf)); w.Write(CS_CMD_HBB(ef,hr,mn)); w.Write(static_cast<uint32_t>(0));
                } else {
                    uint16_t base=csReader.ReadUInt16(),sf=csReader.ReadUInt16();
                    uint16_t ef=csReader.ReadUInt16(),f3=csReader.ReadUInt16();
                    w.Write(CS_CMD_HH(base,sf)); w.Write(CS_CMD_HH(ef,f3));
                    bool isAC = (cid!=0x03&&cid!=0x04&&cid!=0x56&&cid!=0x57&&cid!=0x7C);
                    if (isAC) {
                        uint16_t ry=csReader.ReadUInt16(),rz=csReader.ReadUInt16();
                        w.Write(CS_CMD_HH(ry,rz));
                        for (int j=0;j<9;j++) w.Write(csReader.ReadUInt32());
                    } else {
                        for (int j=0;j<10;j++) w.Write(csReader.ReadUInt32());
                    }
                }
            }
        }
    }

    w.Write(static_cast<uint32_t>(0xFFFFFFFF)); w.Write(static_cast<uint32_t>(0));
    uint32_t endPos = w.GetStream()->GetLength();
    w.Seek(sizePos, LUS::SeekOffsetType::Start);
    w.Write(static_cast<uint32_t>((endPos - startPos) / 4));
    w.Seek(endPos, LUS::SeekOffsetType::Start);

    std::stringstream ss;
    w.Finish(ss);
    std::string str = ss.str();
    return std::vector<char>(str.begin(), str.end());
}

class OoTCutsceneData : public IParsedData {
public:
    std::vector<char> mBinary;
    OoTCutsceneData(std::vector<char> data) : mBinary(std::move(data)) {}
};

std::optional<std::shared_ptr<IParsedData>> OoTCutsceneFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto data = SerializeCutscene(buffer, GetSafeNode<uint32_t>(node, "offset"));
    if (data.empty()) {
        SPDLOG_WARN("OoTCutsceneFactory: Failed to serialize cutscene");
        return std::nullopt;
    }
    return std::make_shared<OoTCutsceneData>(std::move(data));
}

ExportResult OoTCutsceneBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto cs = std::static_pointer_cast<OoTCutsceneData>(raw);
    write.write(cs->mBinary.data(), cs->mBinary.size());
    return std::nullopt;
}

} // namespace OoT

#endif
