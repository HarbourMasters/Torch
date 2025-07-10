#include "BKAssetFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <iomanip>
#include <yaml-cpp/yaml.h>
#include <cstring>

namespace BK64 {

static const std::unordered_map<BKAssetType, std::string> sAssetSymbolPrefixes = {
    { BKAssetType::Animation, "ANIM" },
    { BKAssetType::Binary, "BIN" },
    { BKAssetType::DemoInput, "DEMO" },
    { BKAssetType::Dialog, "DIALOG" },
    { BKAssetType::GruntyQuestion, "GRUNTYQ" },
    { BKAssetType::LevelSetup, "LEVEL_SETUP" },
    { BKAssetType::Midi, "MIDI" },
    { BKAssetType::Model, "MODEL" },
    { BKAssetType::QuizQuestion, "QUIZQ" },
    { BKAssetType::Sprite, "SPRITE" },
};

ExportResult BKAssetHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {

    return std::nullopt;
}

ExportResult BKAssetCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto assetTable = std::static_pointer_cast<BKAssetData>(raw);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    // TODO: Write Out Asset Table

    return offset;
}

ExportResult BKAssetBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    // No Need To Export The Asset Table
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> BKAssetFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    bool symbolMapExists = false;
    if (node["symbol_map"]) {
        symbolMapExists = true;
    }
    
    reader.SetEndianness(Torch::Endianness::Big);
    
    uint32_t assetCount = reader.ReadUInt32();
    reader.ReadUInt32();

    uint32_t dataStartRomOffset = offset + 8 + assetCount * 8;
    int16_t prevTFlag = 3;
    int32_t assetMode = 0;

    std::vector<BKAssetInfo> assetTableInfo;
    
    for (uint32_t i = 0; i < assetCount; i++) {
        BKAssetInfo assetInfo;
        assetInfo.index = i;
        assetInfo.offset = reader.ReadUInt32();
        assetInfo.compressionFlag = reader.ReadInt16();
        assetInfo.tFlag = reader.ReadInt16();

        if (assetInfo.tFlag == 4) {
            // Empty Asset Slot
            // May Need to add in order to get offset size
            assetTableInfo.emplace_back(assetInfo);
            continue;
        }

        if (assetInfo.tFlag != 2 && (prevTFlag & 2) != (assetInfo.tFlag & 2)) {
            assetMode++;
            prevTFlag = assetInfo.tFlag;
        }

        assetInfo.assetMode = assetMode;

        assetTableInfo.emplace_back(assetInfo);
    }

    int count = 0;

    for (uint32_t i = 0; i < assetCount - 1; i++) {
        auto assetInfo = assetTableInfo.at(i);
        auto assetSize = assetTableInfo.at(i + 1).offset - assetInfo.offset;
        auto assetOffset = dataStartRomOffset + assetInfo.offset;
        BKAssetType assetType;

        if (assetInfo.tFlag == 4) {
            continue;
        }

        switch (assetInfo.assetMode) {
            case 0:
                assetType = BKAssetType::Animation;
                break;
            case 1:
            case 3: {
                uint8_t* dataBuf;
                if (assetInfo.compressionFlag != 0) {
                    DataChunk* uncompressedData = Decompressor::Decode(buffer, assetOffset, CompressionType::BKZIP, assetSize);
                    dataBuf = uncompressedData->data;
                } else {
                    dataBuf = buffer.data() + assetOffset;
                }
                if (dataBuf[0] == 0 && dataBuf[1] == 0 && dataBuf[2] == 0 && dataBuf[3] == 11) {
                    assetType = BKAssetType::Model;
                } else {
                    assetType = BKAssetType::Sprite;
                }
                break;
            }
            case 2:
                assetType = BKAssetType::LevelSetup;
                break;
            case 4: {
                uint8_t* dataBuf;
                if (assetInfo.compressionFlag != 0) {
                    DataChunk* uncompressedData = Decompressor::Decode(buffer, assetOffset, CompressionType::BKZIP, assetSize);
                    dataBuf = uncompressedData->data;
                } else {
                    dataBuf = buffer.data() + assetOffset;
                }
                if (dataBuf[0] == 1 && dataBuf[1] == 1 && dataBuf[2] == 2 && dataBuf[3] == 5 && dataBuf[4] == 0) {
                    assetType = BKAssetType::QuizQuestion;
                } else if (dataBuf[0] == 1 && dataBuf[1] == 3 && dataBuf[2] == 0 && dataBuf[3] == 5 && dataBuf[4] == 0) {
                    assetType = BKAssetType::GruntyQuestion;
                } else if (dataBuf[0] == 1 && dataBuf[1] == 3 && dataBuf[2] == 0) {
                    assetType = BKAssetType::Dialog;
                } else {
                    assetType = BKAssetType::DemoInput;
                }
                break;
            }
            case 5:
                assetType = BKAssetType::Model;
                break;
            case 6:
                assetType = BKAssetType::Midi;
                break;
            default:
                assetType = BKAssetType::Binary;
                break;
        }

        std::string assetSymbol;
        std::string assetIndexStr = std::to_string(assetInfo.index);

        if (symbolMapExists && node["symbol_map"][assetIndexStr]) {
            assetSymbol = node["symbol_map"][assetIndexStr].as<std::string>();
        } else {
            std::stringstream assetStream;

            assetStream << "D_" << sAssetSymbolPrefixes.at(assetType) << "_" << std::to_string(assetInfo.index);

            assetSymbol = assetStream.str();
        }

        YAML::Node bkAssetNode;
        bkAssetNode["offset"] = assetOffset;
        bkAssetNode["symbol"] = assetSymbol;
        if (assetInfo.compressionFlag != 0) {
            bkAssetNode["bkzip"] = true;
            bkAssetNode["compressed_size"] = assetSize;
        }

        // Uncomment AddAssets when Factory is implemented
        switch (assetType) {
            case BKAssetType::Animation:
                bkAssetNode["type"] = "BK64:ANIM";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::Binary:
                bkAssetNode["type"] = "BLOB";
                Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::DemoInput:
                bkAssetNode["type"] = "BK64:DEMO";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::Dialog:
                bkAssetNode["type"] = "BK64:DIALOG";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::GruntyQuestion:
                bkAssetNode["type"] = "BK64:GRUNTYQ";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::LevelSetup:
                bkAssetNode["type"] = "BK64:LEVEL_SETUP";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::Midi:
                bkAssetNode["type"] = "BK64:MIDI";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::Model:
                bkAssetNode["type"] = "BK64:MODEL";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::QuizQuestion:
                bkAssetNode["type"] = "BK64:QUIZQ";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            case BKAssetType::Sprite:
                bkAssetNode["type"] = "BK64:SPRITE";
                // Companion::Instance->AddAsset(bkAssetNode);
                break;
            default:
                // Should be unreachable
                throw std::runtime_error("Invalid BKAsset Type Found");
        }
    }

    return std::make_shared<BKAssetData>(assetTableInfo);
}

} // namespace BK64
