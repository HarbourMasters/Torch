#include "BKAssetFactory.h"
#include "BKAssetRareZip.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <iomanip>
#include <yaml-cpp/yaml.h>
#include <cstring>

ExportResult BinaryAssetHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern u8 " << symbol << "[];\n";
    write << "extern size_t " << symbol << "_size;\n";

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
    write << "// - compressed: " << (asset->mCompressed ? "true" : "false") << "\n";
    write << "// - t_flag: " << asset->mTFlag << "\n";
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
    WriteHeader(writer, Torch::ResourceType::Blob, 2);

    // Write metadata
    writer.Write((uint32_t) asset->mSubtype.size());
    writer.Write(asset->mSubtype.c_str(), asset->mSubtype.size());
    writer.Write((uint8_t) (asset->mCompressed ? 1 : 0));
    writer.Write((uint32_t) asset->mTFlag);

    // Write buffer size and the actual buffer data
    writer.Write((uint32_t) asset->mBuffer.size());
    writer.Write((char*) asset->mBuffer.data(), asset->mBuffer.size());
    
    writer.Finish(write);
    return std::nullopt;
}

ExportResult BinaryAssetXMLExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto asset = std::static_pointer_cast<BinaryAssetData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    
    *replacement += ".meta";

    // Build YAML metadata output
    YAML::Emitter out;
    out << YAML::BeginMap;
    
    out << YAML::Key << "BinaryAsset" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << symbol;
    out << YAML::Key << "size" << YAML::Value << asset->mBuffer.size();
    out << YAML::Key << "subtype" << YAML::Value << asset->mSubtype;
    out << YAML::Key << "compressed" << YAML::Value << (asset->mCompressed ? "true" : "false");
    out << YAML::Key << "t_flag" << YAML::Value << asset->mTFlag;
    out << YAML::EndMap;
    
    out << YAML::EndMap;
    
    write << out.c_str();
    
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> BinaryAssetFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // Extract the asset properties from the YAML node
    auto subtype = GetSafeNode<std::string>(node, "subtype", "");
    auto compressed = GetSafeNode<bool>(node, "compressed", false);
    auto tFlag = GetSafeNode<int>(node, "t_flag", 0);
    
    // Get size of the asset
    auto size = GetSafeNode<size_t>(node, "size");
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    
    // Create a pointer to the data at the specified offset
    const u8* srcData = buffer.data() + ASSET_PTR(offset);
    std::vector<uint8_t> decompressedData;
    
    // Handle the data based on whether it's compressed or not
    if (compressed) {
        // Handle Rare's custom compression
        SPDLOG_INFO("Handling BK64 compressed asset at offset 0x{:X}", offset);
        
        // Create a RareZip decompressor
        RareZip::Decompressor decompressor;
        
        // Get uncompressed size from the header
        int uncompressedSize = RareZip::Decompressor::GetUncompressedSize(srcData);
        SPDLOG_INFO("Expected uncompressed size: {}", uncompressedSize);
        
        // Decompress using rarezip
        decompressedData = decompressor.Decompress(srcData, uncompressedSize);
        
        SPDLOG_INFO("Decompressed BK64 asset: {} bytes -> {} bytes", 
                   size, decompressedData.size());
    } else {
        // No decompression needed, just copy the data
        SPDLOG_INFO("No decompression for asset at offset 0x{:X}", offset);
        
        decompressedData = std::vector<uint8_t>(srcData, srcData + size);
    }
    
    SPDLOG_INFO("Parsed binary asset: subtype={}, compressed={}, t_flag={}, size={}", 
                subtype, compressed, tFlag, decompressedData.size());
    
    // Create the binary asset data
    return std::make_shared<BinaryAssetData>(
        decompressedData,
        subtype,
        compressed,
        tFlag
    );
}

std::optional<std::shared_ptr<IParsedData>> BinaryAssetFactory::parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // For modding, we take the raw buffer and properties
    auto subtype = GetSafeNode<std::string>(node, "subtype", "");
    auto compressed = GetSafeNode<bool>(node, "compressed", false);
    auto tFlag = GetSafeNode<int>(node, "t_flag", 0);
    
    // For compressed assets, we might want to handle compression here
    // For now, we assume the input buffer is already in the format we want
    
    return std::make_shared<BinaryAssetData>(buffer, subtype, compressed, tFlag);
}