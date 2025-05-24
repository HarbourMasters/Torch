#include "BKAnimFactory.h"
//#include "librarezip.h"
#include "BKAssetRareZip.h"

#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include "types/RawBuffer.h"

// just a note: bk64 usv1.0/pal assets.bin offset = 0x5E90

namespace BK64 {

ExportResult AnimHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern BK64::Asset " << symbol << ";\n";

    return std::nullopt;
}

ExportResult AnimCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                       YAML::Node& node, std::string* replacement) {
    /* example minimal yaml entry from SF64
    D_TI1_7006F74:
    { type: SF64:ANIM, offset: 0x7006F74, symbol: D_TI1_7006F74 }
    */
    /*asset from BK64 usv1.,
    anim_002e:
    {type: BK64:BINARY, offset: 0x0000c528, symbol: 002e, compressed: 1, size: 6488, t_flag: 3, subtype:
    BK64::ANIMATION}
    */
   auto symbol = GetSafeNode(node, "symbol", entryName);
   auto offset = GetSafeNode<uint32_t>(node, "offset");
   auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

   if(Companion::Instance->IsOTRMode()){
       write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
       return std::nullopt;
   }

   write << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[] = {\n" << tab_t;

   for (int i = 0; i < data.size(); i++) {
       if ((i % 15 == 0) && i != 0) {
           write << "\n" << tab_t;
       }

       write << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int) data[i] << ", ";
   }
   write << "\n};\n";

   if (Companion::Instance->IsDebug()) {
       write << "// size: 0x" << std::hex << std::uppercase << data.size() << "\n";
   }

   return offset + data.size();
}

/*
typedef struct animation_file_s { //*SkeletalAnimation::animation_bin [xx xx yy yy zz zz 00 00]
  s16 unk0; //start frame of animation, [xx xx]
  s16 unk2; // end frame of animation, [yy yy]
  s16 elem_cnt; // number of elements in the animation, [zz zz]
  u8 pad6[2]; // padding
} AnimationFile;
*/

ExportResult BK64::AnimBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    /* example minimal yaml entry from SF64
    D_TI1_7006F74:
    { type: SF64:ANIM, offset: 0x7006F74, symbol: D_TI1_7006F74 }
    */
    /*asset from BK64 usv1.0
    anim_002e:
    {type: BK64:BINARY, offset: 0x0000c528, symbol: 002e, compressed: 1, size: 6488, t_flag: 3, subtype:
    BK64::ANIMATION}
    */
    auto writer = LUS::BinaryWriter();
    const auto data = std::static_pointer_cast<AnimData>(raw);
    const AnimationInfo* info = &data->mAnimInfo;
    WriteHeader(writer, Torch::ResourceType::BKAnimation, 0);
    writer.Write(info->mFile.unk0);
    writer.Write(info->mFile.unk2);
    writer.Write(info->mFile.elem_cnt);
    for (size_t i = 0; i < (size_t)info->mFile.elem_cnt; i++) {
        const AnimationFileElement* elem = &info->mElement[i];
        writer.Write(elem->unk0_15);
        writer.Write(elem->unk0_3);
        writer.Write(elem->data_cnt);
        for (size_t j = 0; j < elem->data_cnt; j++) {
            const AnimationFileData* fileData = &elem->data[j];
            writer.Write(fileData->unk0_15);
            writer.Write(fileData->unk0_14);
            writer.Write(fileData->unk0_13);
            writer.Write(fileData->unk2);
        }
    }
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> AnimFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto size = GetSafeNode<size_t>(node, "size");
    auto compressed = GetSafeNode<int>(node, "compressed", 0);
    auto tFlag = GetSafeNode<int>(node, "t_flag", 0);
    auto index = GetSafeNode<int>(node, "index", 0);
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    uint8_t* srcData = buffer.data() + ASSET_PTR(offset);
    std::vector<uint8_t> processedData;
    if (compressed) {
        processedData = bk_unzip(srcData, size);
    } else {
        processedData = std::vector<uint8_t>(srcData, srcData + size);
    }
    LUS::BinaryReader reader(processedData.data(), processedData.size());
    reader.SetEndianness(Torch::Endianness::Big);

    auto animData = std::make_shared<AnimData>();
    // AnimationFile
    animData->mIsCompressed = compressed;
    animData->mAssetFlag = tFlag;
    animData->index = index;
    animData->mAnimInfo.mFile.unk0 = reader.ReadInt16();
    animData->mAnimInfo.mFile.unk2 = reader.ReadInt16();
    animData->mAnimInfo.mFile.elem_cnt = reader.ReadInt16();
    (void)reader.ReadUInt16();
    // AnimationFileData
    animData->mAnimInfo.mElement = new AnimationFileElement[animData->mAnimInfo.mFile.elem_cnt];
    for (size_t i = 0; i < (size_t)animData->mAnimInfo.mFile.elem_cnt; i++) {
        AnimationFileElement* elem = &animData->mAnimInfo.mElement[i];
        uint16_t firstHalfWord = ( reader.ReadUInt16());

        elem->unk0_15 = (firstHalfWord & 0b111111111110000) >> 4;
        elem->unk0_3 = firstHalfWord &  (0b0000000000001111);
        elem->data_cnt = reader.ReadInt16();
        elem->data = new AnimationFileData[elem->data_cnt];
        for (size_t j = 0; j < elem->data_cnt; j++) {
            AnimationFileData* data = &elem->data[j];
            uint16_t firstHalfWord = reader.ReadUInt16();
            data->unk0_15 = (firstHalfWord & 0b1000000000000000) >> 15;
            data->unk0_14 = (firstHalfWord & 0b0100000000000000) >> 14;
            data->unk0_13 = firstHalfWord & 0b001111111111111;
            data->unk2 = reader.ReadInt16();
        }
    }
    return animData;
}

AnimData::~AnimData() {
    for (size_t i = 0; i < mAnimInfo.mFile.elem_cnt; i++) {
        AnimationFileElement* elem = &mAnimInfo.mElement[i];
        delete[] elem->data;
    }
    delete[] mAnimInfo.mElement;
}

} // namespace BK64