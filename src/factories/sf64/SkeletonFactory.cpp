#include "SkeletonFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(7) << x
#define NUM_JOINT(x) std::dec << std::setfill(' ') << std::setw(5) << x

SF64::LimbData::LimbData(uint32_t addr, uint32_t dList, float tx, float ty, float tz, float rx, float ry, float rz, uint32_t sibling, uint32_t child, int index): mAddr(addr), mDList(dList), mTx(tx), mTy(ty), mTz(tz), mRx(rx), mRy(ry), mRz(rz), mSibling(sibling), mChild(child), mIndex(index) {
    
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

    limbDict[0] = "NULL";
    for(SF64::LimbData limb : limbs) {
        std::ostringstream limbDefaultName;
        auto limbOffset = limb.mAddr;
        if(IS_SEGMENTED(limbOffset)){
            limbOffset = SEGMENT_OFFSET(limbOffset);
        }
        limbDefaultName << symbol << "_limb_" << std::dec << limb.mIndex << "_" << std::hex << limbOffset;
        limbDict[limb.mAddr] = limbDefaultName.str();
    }

    for(SF64::LimbData limb : limbs) {
        write << "Limb " << limbDict[limb.mAddr] << " = {\n";
        
        if(limb.mDList != 0) {
            write << fourSpaceTab << "0x" << std::hex << limb.mDList << ", ";
        } else {
            write << fourSpaceTab << "NULL, ";
        }
        write << "{ " << limb.mTx << ", " << limb.mTy << ", " << limb.mTz << "}, ";
        write << "{ " << std::dec << limb.mRx << ", " << limb.mRy << ", " << limb.mRz << "}, ";
        write << ((limb.mSibling != 0) ? "&" : "") << limbDict[limb.mSibling] << ", ";
        write << ((limb.mChild != 0) ? "&" : "") << limbDict[limb.mChild] << ",\n";
        write << "};\n\n";
    }

    write << "Limb* " << symbol << "[] = {";
    for(int i = 0; i <= skeleton->mSkeleton.size(); i++) {
        if ((i % 6) == 0) {
            write << "\n" << fourSpaceTab;
        }
        if (i == skeleton->mSkeleton.size()) {
            write << "NULL, ";
        } else {
            write << "&" << limbDict[skeleton->mSkeleton[i].mAddr] << ", ";
        }
    }
    write << "\n};\n\n";
}

void SF64::SkeletonBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    // auto anim = std::static_pointer_cast<AnimData>(raw);
    // auto writer = LUS::BinaryWriter();

    // WriteHeader(writer, LUS::ResourceType::AnimData, 0);
    // writer.Write(anim->mFrameCount);
    // writer.Write(anim->mLimbCount);
    // writer.Write((uint32_t) anim->mFrameData.size());
    // writer.Write((uint32_t) anim->mJointKeys.size());

    // for(auto joint : anim->mJointKeys) {
    //     for (int i = 0; i < 6; i++) {
    //         writer.Write(joint.keys[i]);
    //     }
    // }
    // for(auto data : anim->mFrameData) {
    //     writer.Write(data);
    // }
    // writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> SF64::SkeletonFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    YAML::Node limbNode;
    std::vector<SF64::LimbData> skeleton;
    std::map<std::string, std::string> limbDict;
    auto [root, segment] = Decompressor::AutoDecode(node, buffer, 0x1000);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);
    auto limbAddr = reader.ReadUInt32();
    auto limbIndex = 0;
    

    while(limbAddr != 0) {
        limbNode["offset"] = limbAddr;
        // std::cout << std::dec << limbIndex << ": 0x" << std::hex << limbAddr << "\n";
        DecompressedData limbDataRaw = Decompressor::AutoDecode(limbNode, buffer, 0x20);
        LUS::BinaryReader limbReader(limbDataRaw.segment.data, limbDataRaw.segment.size);
        limbReader.SetEndianness(LUS::Endianness::Big);

        auto dListAddr = limbReader.ReadUInt32();
        auto tx = limbReader.ReadFloat();
        auto ty = limbReader.ReadFloat();
        auto tz = limbReader.ReadFloat();
        auto rx = limbReader.ReadInt16();
        auto ry = limbReader.ReadInt16();
        auto rz = limbReader.ReadInt16();
        auto rw = limbReader.ReadInt16();
        auto siblingAddr = limbReader.ReadUInt32();
        auto childAddr = limbReader.ReadUInt32();

        skeleton.push_back(LimbData(limbAddr, dListAddr, tx, ty, tz, rx, ry, rz, siblingAddr, childAddr, limbIndex));
        limbAddr = reader.ReadUInt32();
        limbIndex++;
    }

    return std::make_shared<SF64::SkeletonData>(skeleton);
}
