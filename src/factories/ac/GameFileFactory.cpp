#include "GameFileFactory.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace AC {
namespace {

struct Specification {
    std::string_view role;
    std::string_view archivePath;
    uint64_t sourceOffset;
    uint32_t logicalSize;
    uint32_t storedSize;
};

constexpr uint32_t Align32(uint32_t value) {
    return (value + 31U) & ~31U;
}

constexpr std::array<Specification, 11> kSpecifications{ {
    { "dol", "ac/executable/main.dol", 122880, 918720, 918720 },
    { "dvd", "ac/dvd/audiorom.img", 1438854976, 8300384, Align32(8300384) },
    { "dvd", "ac/dvd/COPYDATE", 1447155360, 19, Align32(19) },
    { "dvd", "ac/dvd/famicom.arc", 1458278336, 1699904, Align32(1699904) },
    { "dvd", "ac/dvd/foresta.map", 1433446456, 4849144, Align32(4849144) },
    { "dvd", "ac/dvd/foresta.rel.szs", 1447155436, 6137393, Align32(6137393) },
    { "dvd", "ac/dvd/forest_1st.arc", 1453292832, 852896, Align32(852896) },
    { "dvd", "ac/dvd/forest_2nd.arc", 1454145728, 4132608, Align32(4132608) },
    { "dvd", "ac/dvd/opening.bnr", 1438295600, 6496, Align32(6496) },
    { "dvd", "ac/dvd/static.map", 1438302096, 552879, Align32(552879) },
    { "dvd", "ac/dvd/static.str", 1447155380, 56, Align32(56) },
} };

std::string NormalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    constexpr std::string_view prefix = "__OTR__";
    if (path.rfind(prefix, 0) == 0) {
        path.erase(0, prefix.size());
    }
    return path;
}

const Specification& RequireSpecification(YAML::Node& node) {
    if (node["source_base_offset"]) {
        throw std::runtime_error("AC:GAME_FILE does not accept source_base_offset");
    }
    if (GetSafeNode<uint32_t>(node, "offset") != 0) {
        throw std::runtime_error("AC:GAME_FILE generic offset must be packed offset 0");
    }
    const std::string role = GetSafeNode<std::string>(node, "role");
    const std::string path = NormalizePath(GetSafeNode<std::string>(node, "destination_path"));
    const auto found = std::find_if(kSpecifications.begin(), kSpecifications.end(), [&](const Specification& item) {
        return item.role == role && item.archivePath == path;
    });
    if (found == kSpecifications.end()) {
        throw std::runtime_error("AC:GAME_FILE role and destination_path are not a supported game file");
    }
    const uint32_t logicalSize = GetSafeNode<uint32_t>(node, "logical_size");
    const uint32_t storedSize = GetSafeNode<uint32_t>(node, "stored_size");
    if (logicalSize != found->logicalSize || storedSize != found->storedSize) {
        throw std::runtime_error("AC:GAME_FILE logical_size or stored_size differs from the supported family");
    }
    if ((role == "dol" && storedSize != logicalSize) || (role == "dvd" && storedSize != Align32(logicalSize))) {
        throw std::runtime_error("AC:GAME_FILE role storage policy is invalid");
    }
    const auto ranges = node["bounded_ranges"];
    if (!ranges || !ranges.IsSequence() || ranges.size() != 1) {
        throw std::runtime_error("AC:GAME_FILE requires exactly one bounded source range");
    }
    auto range = ranges[0];
    if (!range.IsMap() || range.size() != 3 || GetSafeNode<uint64_t>(range, "source_offset") != found->sourceOffset ||
        GetSafeNode<uint64_t>(range, "size") != found->storedSize ||
        GetSafeNode<uint64_t>(range, "packed_offset") != 0) {
        throw std::runtime_error("AC:GAME_FILE bounded source range differs from the supported family");
    }
    return *found;
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> GameFileFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto& specification = RequireSpecification(node);
    if (buffer.size() != specification.storedSize) {
        throw std::runtime_error("AC:GAME_FILE packed input size differs from stored_size");
    }
    auto data = std::make_shared<GameFileData>();
    data->bytes = buffer;
    data->archivePath = std::string(specification.archivePath);
    return data;
}

ExportResult GameFileBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto data = std::static_pointer_cast<GameFileData>(raw);
    const auto& specification = RequireSpecification(node);
    if (data->archivePath != specification.archivePath || data->bytes.size() != specification.storedSize) {
        throw std::runtime_error("AC:GAME_FILE export shape is not exact");
    }
    entryName = data->archivePath;
    if (replacement != nullptr) {
        *replacement = entryName;
    }
    write.write(reinterpret_cast<const char*>(data->bytes.data()), static_cast<std::streamsize>(data->bytes.size()));
    if (!write) {
        throw std::runtime_error("AC:GAME_FILE raw export failed");
    }
    return data->bytes.size();
}

} // namespace AC
