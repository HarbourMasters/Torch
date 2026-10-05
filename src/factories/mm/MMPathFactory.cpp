#include "MMPathFactory.h"

#include "MMUtils.h"

#include <sstream>

namespace MM {

class MMPathData : public IParsedData {
public:
    std::vector<char> binary;
    explicit MMPathData(std::vector<char> data) : binary(std::move(data)) {}
};

std::optional<std::shared_ptr<IParsedData>> MMPathFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const uint32_t offset = GetSafeNode<uint32_t>(node, "offset");
    if (node["paths"]) {
        const auto paths = node["paths"];

        LUS::BinaryWriter writer;
        BaseExporter::WriteHeader(writer, Torch::ResourceType::OoTPath, 0);
        writer.Write(static_cast<uint32_t>(paths.size()));

        for (const auto& path : paths) {
            const auto points = path["points"];
            writer.Write(static_cast<uint32_t>(points.size()));
            writer.Write(path["additional_path_index"].as<uint8_t>());
            writer.Write(path["custom_value"].as<int16_t>());
            for (const auto& point : points) {
                writer.Write(point[0].as<int16_t>());
                writer.Write(point[1].as<int16_t>());
                writer.Write(point[2].as<int16_t>());
            }
        }

        std::stringstream stream;
        writer.Finish(stream);
        const std::string serialized = stream.str();
        return std::make_shared<MMPathData>(std::vector<char>(serialized.begin(), serialized.end()));
    }

    const uint32_t numPaths = GetSafeNode<uint32_t>(node, "num_paths", 1);
    auto paths = ReadSubArray(buffer, offset, numPaths * 8);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::OoTPath, 0);
    writer.Write(numPaths);

    for (uint32_t i = 0; i < numPaths; ++i) {
        const uint8_t pointCount = paths.ReadUByte();
        const uint8_t additionalPathIndex = paths.ReadUByte();
        const int16_t customValue = paths.ReadInt16();
        const uint32_t pointsAddress = paths.ReadUInt32();

        if (pointsAddress == 0 || !IS_SEGMENTED(pointsAddress)) {
            return std::nullopt;
        }

        writer.Write(static_cast<uint32_t>(pointCount));
        writer.Write(additionalPathIndex);
        writer.Write(customValue);

        auto points = ReadSubArray(buffer, pointsAddress, pointCount * 6);
        for (uint8_t point = 0; point < pointCount; ++point) {
            writer.Write(points.ReadInt16());
            writer.Write(points.ReadInt16());
            writer.Write(points.ReadInt16());
        }
    }

    std::stringstream stream;
    writer.Finish(stream);
    const std::string serialized = stream.str();
    return std::make_shared<MMPathData>(std::vector<char>(serialized.begin(), serialized.end()));
}

ExportResult MMPathBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                          std::string&, YAML::Node&, std::string*) {
    const auto path = std::static_pointer_cast<MMPathData>(raw);
    write.write(path->binary.data(), path->binary.size());
    return std::nullopt;
}

} // namespace MM
