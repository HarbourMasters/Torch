#include "SkeletonFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define NUM(x, w) std::dec << std::setfill(' ') << std::setw(w) << x
// #define NUM_JOINT(x) std::dec << std::setfill(' ') << std::setw(5) << x
#define FLOAT(x, w, p) std::dec << std::setfill(' ') << std::setw(w) << std::fixed << std::setprecision(p) << x

SF64::LimbData::LimbData(uint32_t addr, uint32_t dList, Vec3f trans, Vec3s rot, uint32_t sibling, uint32_t child, int index): mAddr(addr), mDList(dList), mTrans(trans), mRot(rot), mSibling(sibling), mChild(child), mIndex(index) {
    
}

void SF64::SkeletonHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern Limb* " << symbol << "[];\n";
}

void SF64::SkeletonCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto skeleton = std::static_pointer_cast<SF64::SkeletonData>(raw);
    auto limbs = skeleton->mSkeleton;
    std::sort(limbs.begin(), limbs.end(), [](SF64::LimbData a, SF64::LimbData b) {return a.mAddr < b.mAddr;});
    std::map<uint32_t, std::string> limbDict;

    if (Companion::Instance->IsDebug()) {
        int off = limbs[0].mAddr;
        if (IS_SEGMENTED(off)) {
            off = SEGMENT_OFFSET(off);
        }
        write << "// 0x" << std::hex << std::uppercase << off << "\n";
    }
    limbDict[0] = "NULL";
    for(SF64::LimbData limb : limbs) {
        std::ostringstream limbDefaultName;
        auto limbOffset = limb.mAddr;
        if(IS_SEGMENTED(limbOffset)){
            limbOffset = SEGMENT_OFFSET(limbOffset);
        }
        limbDefaultName << symbol << "_limb_" << std::dec << limb.mIndex << "_" << std::uppercase << std::hex << limbOffset;
        limbDict[limb.mAddr] = limbDefaultName.str();
    }

    for(SF64::LimbData limb : limbs) {
        write << "Limb " << limbDict[limb.mAddr] << " = {\n";
        write << fourSpaceTab;
        if(limb.mDList == 0) {
            write << "NULL, ";
        } else {
            auto dec = Companion::Instance->GetNodeByAddr(limb.mDList);
            if(dec.has_value()){
                auto node = std::get<1>(dec.value());
                auto symbol = GetSafeNode<std::string>(node, "symbol");
                write << symbol << ", ";
            } else {
                write << "0x" << std::uppercase << std::hex << limb.mDList << ", ";
            }
        }
        write << FLOAT(limb.mTrans, 0, limb.mTrans.precision()) << ", ";
        write << std::dec << limb.mRot << ", ";
        write << ((limb.mSibling != 0) ? "&" : "") << limbDict[limb.mSibling] << ", ";
        write << ((limb.mChild != 0) ? "&" : "") << limbDict[limb.mChild] << ",\n";
        write << "};\n\n";
    }

    write << "Limb* " << symbol << "[] = {";
    for(int i = 0; i <= skeleton->mSkeleton.size(); i++) {
        if ((i % 4) == 0) {
            write << "\n" << fourSpaceTab;
        }
        if (i == skeleton->mSkeleton.size()) {
            write << "NULL, ";
        } else {
            write << "&" << limbDict[skeleton->mSkeleton[i].mAddr] << ", ";
        }
    }
    write << "\n};\n";
    if (Companion::Instance->IsDebug()) {
        int sz = (skeleton->mSkeleton.size() * 0x24) + 4;
        int off = limbs[0].mAddr;;
        if (IS_SEGMENTED(off)) {
            off = SEGMENT_OFFSET(off);
        }
        write << "// Limbs: " << std::dec << skeleton->mSkeleton.size() << "\n";
        write << "// 0x" << std::hex << std::uppercase << (off + sz) << "\n";
    }
    write << "\n";
}

void SF64::SkeletonBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {

}

std::optional<std::shared_ptr<IParsedData>> SF64::SkeletonFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    std::vector<SF64::LimbData> skeleton;
    std::map<std::string, std::string> limbDict;
    auto [root, segment] = Decompressor::AutoDecode(node, buffer, 0x1000);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);
    auto limbAddr = reader.ReadUInt32();
    auto limbIndex = 0;
    

    while(limbAddr != 0) {
        YAML::Node limbNode;
        Vec3f trans;
        Vec3s rot;
        limbNode["offset"] = limbAddr;
        DecompressedData limbDataRaw = Decompressor::AutoDecode(limbNode, buffer, 0x20);
        LUS::BinaryReader limbReader(limbDataRaw.segment.data, limbDataRaw.segment.size);
        limbReader.SetEndianness(LUS::Endianness::Big);

        auto dListAddr = limbReader.ReadUInt32();
        trans.x = limbReader.ReadFloat();
        trans.y = limbReader.ReadFloat();
        trans.z = limbReader.ReadFloat();
        rot.x = limbReader.ReadInt16();
        rot.y = limbReader.ReadInt16();
        rot.z = limbReader.ReadInt16();
        auto rw = limbReader.ReadInt16();
        auto siblingAddr = limbReader.ReadUInt32();
        auto childAddr = limbReader.ReadUInt32();

        if(dListAddr != 0 && (SEGMENT_NUMBER(dListAddr) == SEGMENT_NUMBER(limbAddr))) {
            YAML::Node dListNode;
            dListNode["type"] = "GFX";
            dListNode["offset"] = dListAddr;
            Companion::Instance->AddAsset(dListNode);
        }

        skeleton.push_back(LimbData(limbAddr, dListAddr, trans, rot, siblingAddr, childAddr, limbIndex));
        limbAddr = reader.ReadUInt32();
        limbIndex++;
    }

    return std::make_shared<SF64::SkeletonData>(skeleton);
}
