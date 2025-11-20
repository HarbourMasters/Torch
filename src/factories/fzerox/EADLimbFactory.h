#pragma once

#include <factories/BaseFactory.h>
#include <types/Vec3D.h>

namespace FZX {

class EADLimbData : public IParsedData {
public:
    uint32_t mDl;
    Vec3f mScale;
    Vec3f mPos;
    Vec3s mRot;
    uint32_t mNextLimb;
    uint32_t mChildLimb;
    uint32_t mAssociatedLimb;
    uint32_t mAssociatedLimbDL;
    int16_t mLimbId;
    
    EADLimbData(uint32_t dl, Vec3f scale, Vec3f pos, Vec3s rot, uint32_t nextLimb, uint32_t childLimb, uint32_t associatedLimb, uint32_t associatedLimbDL, int16_t limbId) :
        mDl(dl),
        mScale(scale),
        mPos(pos),
        mRot(rot),
        mNextLimb(nextLimb),
        mChildLimb(childLimb),
        mAssociatedLimb(associatedLimb),
        mAssociatedLimbDL(associatedLimbDL),
        mLimbId(limbId)
    {}
};

class EADLimbHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EADLimbBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EADLimbCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EADLimbModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class EADLimbFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, EADLimbCodeExporter)
            REGISTER(Header, EADLimbHeaderExporter)
            REGISTER(Binary, EADLimbBinaryExporter)
        };
    }
};
} // namespace FZX
