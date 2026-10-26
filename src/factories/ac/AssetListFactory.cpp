#include "AssetListFactory.h"
#include "ACUtils.h"

#include <strhash64/StrHash64.h>

namespace AC {

ExportResult AssetListBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto& paths = std::static_pointer_cast<AssetListData>(raw)->mPaths;
    auto writer = LUS::BinaryWriter();
    WriteHeader(writer, Torch::ResourceType::AssetArray, 0);
    writer.Write((uint32_t)paths.size());
    for (const auto& path : paths) {
        writer.Write(path.empty() ? (uint64_t)0 : (uint64_t)CRC64(path.c_str()));
    }
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> AssetListFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    if (!node["entries"] || !node["entries"].IsSequence()) {
        throw std::runtime_error(NameOf(node) + ": AC:ASSET_ARRAY needs an `entries` list");
    }
    std::vector<std::string> paths;
    for (const auto& entry : node["entries"]) {
        const auto path = entry.IsNull() ? std::string() : entry.as<std::string>();
        paths.push_back(path == "null" ? std::string() : path);
    }
    return std::make_shared<AssetListData>(paths);
}

} // namespace AC
