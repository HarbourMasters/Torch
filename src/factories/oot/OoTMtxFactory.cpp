#ifdef OOT_SUPPORT

#include "OoTMtxFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

std::optional<std::shared_ptr<IParsedData>> OoTMtxFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 64);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    // Read 16 raw int32 values matching ZAPDTR's ZMtx::ParseRawData format
    std::array<int32_t, 16> rawInts;
    for (size_t i = 0; i < 16; i++) {
        rawInts[i] = reader.ReadInt32();
    }

    return std::make_shared<OoTMtxData>(rawInts);
}

ExportResult OoTMtxBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                          std::string& entryName, YAML::Node& node,
                                          std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<OoTMtxData>(raw);

    WriteHeader(writer, Torch::ResourceType::Matrix, 0);

    for (size_t i = 0; i < 16; i++) {
        writer.Write(data->rawInts[i]);
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
