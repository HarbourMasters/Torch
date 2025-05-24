#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef struct animation_file_s { //*SkeletalAnimation::animation_bin [xx xx yy yy zz zz 00 00]
    int16_t unk0;       //start frame of animation, [xx xx]
    int16_t unk2;       // end frame of animation, [yy yy]
    int16_t elem_cnt;   // number of elements in the animation, [zz zz]
} AnimationFile;

typedef union { //transform structure [xx xx] ⇒ [DE FF FF FF FF FF FF FF]
    struct {
        uint8_t unk0_15 : 1;  // Bitwise Parameters 1 [D]
        uint8_t unk0_14 : 1;  // Bitwise Parameters 2 [E]
        uint16_t unk0_13 : 14; // frame of transformation [FF FF FF FF FF FF FF], somehow 87.59375 (0x15E6) after bitwise operations
        int16_t unk2;
    };
} AnimationFileData;

typedef struct animation_file_elem_s { //element header, [aa ab cc cc], offset 0x8 from bin start
    uint16_t unk0_15 : 12; //internal bone index, [aa a]
    uint8_t unk0_3 : 4;   //transform type [b]
    /*   X Rotation	0
         Y Rotation	1
         Z Rotation	2
         X Scale	3
         Y Scale	4
         Z Scale	5
         X Translation	6
         Y Translation	7
         Z Translation	8 */
    int16_t data_cnt;
    AnimationFileData* data;
} AnimationFileElement;

typedef struct AnimationInfo {
    AnimationFile mFile;
    AnimationFileElement* mElement;
} AnimationInfo;

class AnimData : public IParsedData {
  public:
    ~AnimData();
    AnimationInfo mAnimInfo;
    //int32_t mOffset; //(usv1.0 assets.bin rom offset = 0x5E90) + asset offset = address of asset
    //std::vector<uint8_t> mRawBinary;
    size_t mLength = 0;
    uint16_t index = 0;
    bool mIsCompressed = false;
    int8_t mAssetFlag = 0;

    AnimData() = default;
};

class AnimHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AnimFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { 
            REGISTER(Code, AnimCodeExporter) 
            REGISTER(Header, AnimHeaderExporter)                     
            REGISTER(Binary, AnimBinaryExporter) 
        };
    }
};
} // namespace BK64
