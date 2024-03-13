#include "SkeletonFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define NUM(x) std::dec << std::setfill(' ') << std::setw(7) << x
#define NUM_JOINT(x) std::dec << std::setfill(' ') << std::setw(5) << x

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
        write << limb.mTrans << ", ";
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
        Vec3f trans;
        Vec3s rot;
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
            const auto decl = Companion::Instance->GetNodeByAddr(dListAddr);

            if(!decl.has_value()){
                SPDLOG_INFO("Addr to Display list command at 0x{:X} not in yaml, autogenerating it", dListAddr);

                auto rom = Companion::Instance->GetRomData();
                auto factory = Companion::Instance->GetFactory("GFX")->get();

                std::string output;
                YAML::Node dl;
                uint32_t ptr = dListAddr;

                if(Decompressor::IsSegmented(dListAddr)){
                    SPDLOG_INFO("Found segmented display list at 0x{:X}", dListAddr);
                    output = Companion::Instance->NormalizeAsset("seg" + std::to_string(SEGMENT_NUMBER(ptr)) +"_dl_" + Torch::to_hex(SEGMENT_OFFSET(ptr), false));
                } else {
                    SPDLOG_INFO("Found display list at 0x{:X}", ptr);
                    output = Companion::Instance->NormalizeAsset("dl_" + Torch::to_hex(ptr, false));
                }

                dl["type"] = "GFX";
                dl["offset"] = ptr;
                dl["symbol"] = output;
                dl["autogen"] = true;

                auto result = factory->parse(rom, dl);

                if(!result.has_value()){
                    continue;
                }

                Companion::Instance->RegisterAsset(output, dl);
            } else {
                SPDLOG_WARN("Could not find display list at 0x{:X}", dListAddr);
            }
        }

        skeleton.push_back(LimbData(limbAddr, dListAddr, trans, rot, siblingAddr, childAddr, limbIndex));
        limbAddr = reader.ReadUInt32();
        limbIndex++;
    }

    return std::make_shared<SF64::SkeletonData>(skeleton);
}
