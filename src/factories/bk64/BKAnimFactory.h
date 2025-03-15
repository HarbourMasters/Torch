/* #pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef union {
    struct {     //[xx xx] ⇒ [DE FF FF FF FF FF FF FF]
        uint8_t unk0_15 : 1;   //unknown1 D, supposed to be bool?
        uint8_t unk0_14 : 1;   //unknown2 E,  supposed to be bool?
        uint64_t unk0_13 : 14; //frame of transformation FF FF FF FF FF FF FF
        int8_t unk2;           //padding?    0x0000/ [00 00]
    };
} AnimationFileData; // represents the transformation on a given model bone
Example (60f0.bin, Banjo's Backflip at address 0x0C):
C0 0F 15 E6
At frame 15 (0xF), two bitwise parameters are set as true (if [d] = 1 and [e] = 1, 
then the remaining two bits must return false to equal 0xC) and then the bone transforms by a factor of 87.59375 (0x15E6).


typedef struct animation_file_elem_s {
    uint32_t unk0_15 : 12;  // internal bone ID being targeted 0x000 (A) [AAAb]
    uint8_t unk0_3 : 4;    // transform type  0x0         (B)[aaaB]
                            // 0 =x rotation, 1 = y rotation, 2 = z rotation
                            // 3 = x scale,  4 = y scale, 5 = z scale
                            // 6 = x translation, 7 = y translation, 8 = z translation
    int32_t data_cnt;       // data points affected between frames [cc cc]
    AnimationFileData data[]; //data point currently being affected?
} AnimationFileElement; //offset 0x8 from beginning (aka directly after AnimationFileData), animation data element header

typedef struct animation_file_s { 
    int32_t unk0;       // animationFile_getStartFrame();   [xx xx]
    int32_t unk2;       // animationFile_getEndFrame(); 	[yy yy]
    int32_t elem_cnt;   // animationFile_count(); 	        [zz zz]
    uint8_t pad6[2];    // padding/unknown                  [00 00]
  } AnimationFile;      //0x8 in size //animation data header

//int32_t animation_list_offset_18; from BKModelBin, most likely animationfile->elem_cnt?

class AnimData : public IParsedData {
  public:

    int32_t mOffset;
    int16_t mStartFrame;
    int16_t mEndFrame;
    int16_t mFileCount;
    std::vector<uint8_t> mRawBinary;

    AnimData() = default;

    AnimData(int32_t offset) {
        mOffset = offset;
    }
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
 */