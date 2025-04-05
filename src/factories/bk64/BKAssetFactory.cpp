#include "BKAssetFactory.h"
#include "BKAssetRareZip.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <iomanip>
#include <yaml-cpp/yaml.h>
#include <cstring>

namespace BK64 {
ExportResult BinaryAssetHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern u8 " << symbol << "[];\n";

    return std::nullopt;
}

ExportResult BinaryAssetCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto asset = std::static_pointer_cast<BinaryAssetData>(raw);
    
    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "// Binary Asset with properties:\n";
    write << "// - subtype: " << asset->mSubtype << "\n";
    write << "// - t_flag: " << asset->mTFlag << "\n";
    write << "// - index: " << asset->mIndex << "\n";
    write << "\n";

    write << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[] = {\n" << tab_t;

    for (size_t i = 0; i < asset->mBuffer.size(); i++) {
        if ((i % 15 == 0) && i != 0) {
            write << "\n" << tab_t;
        }

        write << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int) asset->mBuffer[i] << ", ";
    }
    write << "\n};\n";
    
    write << "size_t " << symbol << "_size = " << std::dec << asset->mBuffer.size() << ";\n";

    if (Companion::Instance->IsDebug()) {
        write << "// size: 0x" << std::hex << std::uppercase << asset->mBuffer.size() << "\n";
    }

    return offset + asset->mBuffer.size();
}

ExportResult BinaryAssetBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto asset = std::static_pointer_cast<BinaryAssetData>(raw);

    // Custom resource type
    WriteHeader(writer, Torch::ResourceType::BKBinary, 1);

    // Write metadata
    //writer.Write((uint32_t) asset->mSubtype.size());
    writer.Write(asset->mSubtype.c_str(), asset->mSubtype.size());
    writer.Write((uint32_t) asset->mTFlag);
    writer.Write((uint32_t) asset->mIndex);

    // Write buffer size and the actual buffer data
    writer.Write((uint32_t) asset->mBuffer.size());
    writer.Write((char*) asset->mBuffer.data(), asset->mBuffer.size());
    
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> BinaryAssetFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // Extract the asset properties from the YAML node
    auto subtype = GetSafeNode<std::string>(node, "subtype", "");
    auto fcompressed = GetSafeNode<int>(node, "compressed", 0);
    bool compressed = fcompressed == 0 || fcompressed == 1;
    auto tFlag = GetSafeNode<int>(node, "t_flag", 0);
    auto index = GetSafeNode<int>(node, "index", 0);
    
    // Get size of the compressed asset
    auto size = GetSafeNode<size_t>(node, "size");
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    
    // Create a pointer to the data at the specified offset
    const uint8_t* srcData = buffer.data() + ASSET_PTR(offset);
    std::vector<uint8_t> processedData;

    printf("Processing compressed asset: size=%zu, offset=0x%x\n", size, ASSET_PTR(offset));

    if (compressed) {        
        try {
            processedData = bk_unzip(srcData, size);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Failed to decompress BK64 asset: {}", e.what());
            
            // Optional: If decompression fails, log the first few bytes
            std::string firstBytes;
            for (size_t i = 0; i < std::min(size, size_t(10)); ++i) {
                firstBytes += fmt::format("{:02x} ", srcData[i]);
            }
            SPDLOG_ERROR("First bytes of failed asset: {}", firstBytes);
            
            return std::nullopt;
        }
    } else {
        // If not compressed, use the data as-is
        processedData = std::vector<uint8_t>(srcData, srcData + size);
    }

    SPDLOG_INFO("Parsed binary asset: subtype={}, t_flag={}, size={}, index={}", 
                subtype, tFlag, processedData.size(), index);

    return std::make_shared<BinaryAssetData>(processedData, subtype, tFlag, index);
}

std::optional<std::shared_ptr<IParsedData>> BinaryAssetFactory::parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto subtype = GetSafeNode<std::string>(node, "subtype", "");
    auto compressed = GetSafeNode<bool>(node, "compressed", 0);
    auto tFlag = GetSafeNode<int>(node, "t_flag", 0);
    auto index = GetSafeNode<int>(node, "index", 0);

    // for way later
    
    return std::make_shared<BinaryAssetData>(buffer, subtype, tFlag, index);
}

} // namespace BK64