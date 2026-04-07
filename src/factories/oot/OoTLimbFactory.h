#pragma once

#ifdef OOT_SUPPORT

#include "factories/BaseFactory.h"
#include "OoTSkeletonTypes.h"

namespace OoT {

class OoTLimbBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class OoTLimbFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, OoTLimbBinaryExporter)
        };
    }

private:
    static size_t GetLimbDataSize(OoTLimbType type);
    void ParseLimbHeader(LUS::BinaryReader& reader, OoTLimbData& limb);
    void ParseCurveLimb(LUS::BinaryReader& reader, OoTLimbData& limb, const std::string& symbol);
    void ParseLegacyLimb(LUS::BinaryReader& reader, OoTLimbData& limb, const std::string& symbol);
    void ParseStandardLimb(LUS::BinaryReader& reader, OoTLimbData& limb, const std::string& symbol);
    void ParseLODLimb(LUS::BinaryReader& reader, OoTLimbData& limb, const std::string& symbol);
    void ParseSkinLimb(LUS::BinaryReader& reader, std::vector<uint8_t>& buffer,
                       OoTLimbData& limb, const std::string& symbol);
};

} // namespace OoT

#endif
