#include "EADLimbFactory.h"

#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

#define FORMAT_HEX(x) std::hex << "0x" << std::uppercase << x << std::nouppercase << std::dec
#define FZX_LIMB_SIZE 0x36

ExportResult FZX::EADLimbHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern EADDemoLimb " << symbol << ";\n";

    return std::nullopt;
}

ExportResult FZX::EADLimbCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto limb = std::static_pointer_cast<EADLimbData>(raw);

    write << "EADDemoLimb " << symbol << " = {\n";

    write << fourSpaceTab;

    if (limb->mDl == 0) {
        write << "NULL,\n";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(limb->mDl);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << assetSymbol << ",\n";
        } else {
            write << FORMAT_HEX(limb->mDl) << ",\n";
        }
    }

    write << fourSpaceTab << limb->mScale << ",\n";
    write << fourSpaceTab << limb->mPos << ",\n";
    write << fourSpaceTab << limb->mRot << ",\n";

    write << fourSpaceTab;
    if (limb->mNextLimb == 0) {
        write << "NULL,\n";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(limb->mNextLimb);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << "&" << assetSymbol << ",\n";
        } else {
            write << FORMAT_HEX(limb->mNextLimb) << ",\n";
        }
    }

    write << fourSpaceTab;
    if (limb->mChildLimb == 0) {
        write << "NULL,\n";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(limb->mChildLimb);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << "&" << assetSymbol << ",\n";
        } else {
            write << FORMAT_HEX(limb->mChildLimb) << ",\n";
        }
    }

    write << fourSpaceTab;
    if (limb->mAssociatedLimb == 0) {
        write << "NULL,\n";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(limb->mAssociatedLimb);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << "&" << assetSymbol << ",\n";
        } else {
            write << FORMAT_HEX(limb->mAssociatedLimb) << ",\n";
        }
    }

    write << fourSpaceTab;
    if (limb->mAssociatedLimbDL == 0) {
        write << "NULL,\n";
    } else {
        auto dec = Companion::Instance->GetNodeByAddr(limb->mAssociatedLimbDL);
        if (dec.has_value()) {
            auto node = std::get<1>(dec.value());
            auto assetSymbol = GetSafeNode<std::string>(node, "symbol");
            write << assetSymbol << ",\n";
        } else {
            write << FORMAT_HEX(limb->mAssociatedLimbDL) << ",\n";
        }
    }

    write << fourSpaceTab << limb->mLimbId << "\n};\n\n";

    return offset + FZX_LIMB_SIZE;
}

ExportResult FZX::EADLimbBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto limb = std::static_pointer_cast<EADLimbData>(raw);

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> FZX::EADLimbFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);

    reader.SetEndianness(Torch::Endianness::Big);

    Vec3f scale;
    Vec3f pos;
    Vec3s rot;

    auto dl = reader.ReadUInt32();

    if (dl != 0) {
        YAML::Node dListNode;
        dListNode["type"] = "GFX";
        dListNode["offset"] = dl;
        Companion::Instance->AddAsset(dListNode);
    }
    scale.x = reader.ReadFloat();
    scale.y = reader.ReadFloat();
    scale.z = reader.ReadFloat();
    pos.x = reader.ReadFloat();
    pos.y = reader.ReadFloat();
    pos.z = reader.ReadFloat();
    rot.x = reader.ReadInt16();
    rot.y = reader.ReadInt16();
    rot.z = reader.ReadInt16();
    reader.ReadInt16();
    auto nextLimb = reader.ReadUInt32();
    auto childLimb = reader.ReadUInt32();
    auto associatedLimb = reader.ReadUInt32();
    auto associatedLimbDL = reader.ReadUInt32();
    if (associatedLimbDL != 0) {
        YAML::Node dListNode;
        dListNode["type"] = "GFX";
        dListNode["offset"] = associatedLimbDL;
        Companion::Instance->AddAsset(dListNode);
    }
    auto limbId = reader.ReadInt16();

    return std::make_shared<EADLimbData>(dl, scale, pos, rot, nextLimb, childLimb, associatedLimb, associatedLimbDL, limbId);
}
