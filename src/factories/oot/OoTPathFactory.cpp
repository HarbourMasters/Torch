#ifdef OOT_SUPPORT

#include "OoTPathFactory.h"
#include "OoTSceneUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

namespace OoT {

class OoTPathData : public IParsedData {
public:
    std::vector<char> mBinary;
    OoTPathData(std::vector<char> data) : mBinary(std::move(data)) {}
};

std::optional<std::shared_ptr<IParsedData>> OoTPathFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    uint32_t numPaths = node["num_paths"] ? node["num_paths"].as<uint32_t>() : 1;

    // Each path entry is 8 bytes: numPoints (u8), padding (3 bytes), pointsAddr (u32)
    auto pathReader = ReadSubArray(buffer, offset, numPaths * 8);

    // {numPoints, pointsAddr}
    std::vector<std::pair<uint8_t, uint32_t>> pathways;

    for (uint32_t i = 0; i < numPaths; i++) {
        uint8_t numPoints = pathReader.ReadUByte();
        pathReader.ReadUByte(); // padding
        pathReader.ReadUByte();
        pathReader.ReadUByte();
        uint32_t pointsAddr = pathReader.ReadUInt32();

        if (pointsAddr == 0) break;

        pathways.push_back({numPoints, pointsAddr});
    }

    if (pathways.empty()) return std::nullopt;

    auto data = SerializePathways(buffer, pathways, pathways.size(), 1);
    return std::make_shared<OoTPathData>(std::move(data));
}

ExportResult OoTPathBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto path = std::static_pointer_cast<OoTPathData>(raw);
    write.write(path->mBinary.data(), path->mBinary.size());
    return std::nullopt;
}

} // namespace OoT

#endif
