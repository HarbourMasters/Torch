#include "BKAssetFactory.h"
#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"
#include <cstring>
#include <iomanip>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <yaml-cpp/yaml.h>

namespace BK64 {

static const std::unordered_map<BKAssetType, std::string> sAssetSymbolPrefixes = {
    { BKAssetType::Animation, "ANIM" },
    { BKAssetType::Binary, "BIN" },
    { BKAssetType::DemoInput, "DEMO" },
    { BKAssetType::Dialog, "DIALOG" },
    { BKAssetType::GruntyQuestion, "GRUNTYQ" },
    { BKAssetType::Map, "MAP" },
    { BKAssetType::Midi, "MIDI" },
    { BKAssetType::Model, "MODEL" },
    { BKAssetType::QuizQuestion, "QUIZQ" },
    { BKAssetType::Sprite, "SPRITE" },
};

ExportResult BKAssetHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern BKAssetTableEntry " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult BKAssetCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    auto assetTable = std::static_pointer_cast<BKAssetData>(raw);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "BKAssetTableEntry " << symbol << "[] = {\n";

    for (const auto& assetInfo : assetTable->mAssetTableInfo) {
        write << fourSpaceTab << "{ ";
        write << "/* index */ " << assetInfo.index << ", ";
        write << "/* offset */ " << assetInfo.offset << ", ";
        write << "/* compressed */ " << assetInfo.compressionFlag << ", ";
        write << "/* tFlag */ " << assetInfo.tFlag;
        write << " }, // mode: " << assetInfo.assetMode;

        if (assetInfo.tFlag == 4) {
            write << " (empty slot)";
        }

        write << "\n";
    }

    write << "};\n\n";

    // count + padding + entries
    size_t size = 4 + 4 + (assetTable->mAssetTableInfo.size() * 8);

    return offset + size;
}

ExportResult BKAssetBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto assetTable = std::static_pointer_cast<BKAssetData>(raw);

    WriteHeader(writer, Torch::ResourceType::Blob, 0);

    // Pull the o2r path prefix off our own replacement path, e.g.
    // "assets/aBKAssetTable" -> "assets/"
    std::string prefix;
    if (replacement) {
        auto lastSlash = replacement->rfind('/');
        if (lastSlash != std::string::npos) {
            prefix = replacement->substr(0, lastSlash + 1);
        }
    }

    // Manifest mapping each asset ID to its full o2r path.
    // Format: u32 count, then per entry: u32 assetId, s32 pathLen, char
    // path[pathLen]
    std::vector<std::pair<uint32_t, std::string>> entries;
    entries.reserve(assetTable->mSymbolMap.size() + assetTable->mAssetTableInfo.size());
    size_t payloadSize = 4;
    for (const auto& [id, symbol] : assetTable->mSymbolMap) {
        std::string fullPath = prefix + symbol;
        payloadSize += 4 + 4 + fullPath.size();
        entries.emplace_back(id, std::move(fullPath));
    }
    for (const auto& assetInfo : assetTable->mAssetTableInfo) {
        if (assetInfo.tFlag != 4) {
            continue; // only the empty slots; real assets are already in mSymbolMap
        }
        payloadSize += 4 + 4; // id + zero-length path
        entries.emplace_back(static_cast<uint32_t>(assetInfo.index), std::string());
    }
    writer.Write(static_cast<uint32_t>(payloadSize));

    writer.Write(static_cast<uint32_t>(entries.size()));
    for (const auto& [id, fullPath] : entries) {
        writer.Write(id);
        writer.Write(fullPath);
    }

    writer.Finish(write);
    return OffsetEntry{ 0 };
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
    std::unordered_map<uint32_t, std::string> symbolMap;

    for (uint32_t i = 0; i < assetCount; i++) {
        BKAssetInfo assetInfo;
        assetInfo.index = i;
        assetInfo.offset = reader.ReadUInt32();
        assetInfo.compressionFlag = reader.ReadInt16();
        assetInfo.tFlag = reader.ReadInt16();

        if (assetInfo.tFlag == 4) {
            // Empty slot. Keep it anyway. We still need its offset to size
            // the asset before it.
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

    // Warm the decompressor cache up front, in parallel. The serial parse
    // pass below then mostly hits already-decoded data.
    struct DecompJob {
        uint32_t offset;
        uint32_t size;
    };
    std::vector<DecompJob> decompJobs;
    for (uint32_t i = 0; i < assetCount - 1; i++) {
        auto& ai = assetTableInfo.at(i);
        if (ai.tFlag == 4 || ai.compressionFlag == 0)
            continue;
        uint32_t sz = assetTableInfo.at(i + 1).offset - ai.offset;
        if (sz == 0) {
            for (uint32_t j = i + 2; j < assetCount; j++) {
                if (assetTableInfo.at(j).offset != ai.offset) {
                    sz = assetTableInfo.at(j).offset - ai.offset;
                    break;
                }
            }
        }
        uint32_t off = dataStartRomOffset + ai.offset;
        if (off + sz <= buffer.size()) {
            decompJobs.push_back({ off, sz });
        }
    }

    // Torch already runs us on a worker thread, so leave a core for the parent.
    const unsigned int hwThreads = std::thread::hardware_concurrency();
    const size_t numThreads = hwThreads > 1 ? hwThreads - 1 : 1u;
    SPDLOG_INFO("Pre-decompressing {} assets using {} threads", decompJobs.size(), numThreads);
    auto decompRange = [&](size_t start, size_t end) {
        for (size_t j = start; j < end; j++) {
            try {
                Decompressor::Decode(buffer, decompJobs[j].offset, CompressionType::BKZIP, decompJobs[j].size);
            } catch (...) {}
        }
    };
    std::vector<std::thread> decompThreads;
    size_t decompChunk = (decompJobs.size() + numThreads - 1) / numThreads;
    for (size_t t = 0; t < numThreads; t++) {
        size_t s = t * decompChunk, e = std::min(s + decompChunk, decompJobs.size());
        if (s < e)
            decompThreads.emplace_back(decompRange, s, e);
    }
    for (auto& t : decompThreads)
        t.join();
    SPDLOG_INFO("Pre-decompression complete");

    size_t parseFailures = 0;

    for (uint32_t i = 0; i < assetCount - 1; i++) {
      try {
        auto assetInfo = assetTableInfo.at(i);

        // Size is the gap to the next asset's offset.
        uint32_t assetSize = assetTableInfo.at(i + 1).offset - assetInfo.offset;

        // Same offset means an empty slot sits in between; skip ahead until the
        // offset actually changes to find the real boundary.
        if (assetSize == 0) {
            for (uint32_t j = i + 2; j < assetCount; j++) {
                if (assetTableInfo.at(j).offset != assetInfo.offset) {
                    assetSize = assetTableInfo.at(j).offset - assetInfo.offset;
                    break;
                }
            }
        }

        auto assetOffset = dataStartRomOffset + assetInfo.offset;
        BKAssetType assetType;

        if (assetInfo.tFlag == 4) {
            continue;
        }

        if (assetOffset + assetSize > buffer.size()) {
            SPDLOG_ERROR("Asset {} offset 0x{:X} + size 0x{:X} = 0x{:X} exceeds ROM "
                         "buffer 0x{:X}",
                         assetInfo.index, assetOffset, assetSize, assetOffset + assetSize, buffer.size());
            continue;
        }

        SPDLOG_TRACE("Parsing asset {} mode={} offset=0x{:X} size=0x{:X} comp={}", assetInfo.index, assetInfo.assetMode,
                     assetOffset, assetSize, assetInfo.compressionFlag);

        switch (assetInfo.assetMode) {
            case 0:
                assetType = BKAssetType::Animation;
                break;
            case 1:
            case 3:
            case 7: {
                uint8_t* dataBuf;
                if (assetInfo.compressionFlag != 0) {
                    DataChunk* uncompressedData =
                        Decompressor::Decode(buffer, assetOffset, CompressionType::BKZIP, assetSize);
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
                assetType = BKAssetType::Map;
                break;
            case 4: {
                uint8_t* dataBuf;
                if (assetInfo.compressionFlag != 0) {
                    DataChunk* uncompressedData =
                        Decompressor::Decode(buffer, assetOffset, CompressionType::BKZIP, assetSize);
                    dataBuf = uncompressedData->data;
                } else {
                    dataBuf = buffer.data() + assetOffset;
                }
                // US headers
                if (dataBuf[0] == 1 && dataBuf[1] == 1 && dataBuf[2] == 2 && dataBuf[3] == 5 && dataBuf[4] == 0) {
                    assetType = BKAssetType::QuizQuestion;
                } else if (dataBuf[0] == 1 && dataBuf[1] == 3 && dataBuf[2] == 0 && dataBuf[3] == 5 &&
                           dataBuf[4] == 0) {
                    assetType = BKAssetType::GruntyQuestion;
                } else if (dataBuf[0] == 1 && dataBuf[1] == 3 && dataBuf[2] == 0) {
                    assetType = BKAssetType::Dialog;
                // PAL headers
                } else if (dataBuf[0] == 3 && dataBuf[1] == 1 && dataBuf[2] == 2) {
                    assetType = BKAssetType::QuizQuestion;
                } else if (dataBuf[0] == 3 && dataBuf[1] == 3 && dataBuf[2] == 0) {
                    assetType = BKAssetType::GruntyQuestion;
                } else if (dataBuf[0] == 3 && dataBuf[1] == 7 && dataBuf[2] == 0) {
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

        symbolMap[assetInfo.index] = assetSymbol;

        YAML::Node bkAssetNode;
        bkAssetNode["offset"] = assetOffset;
        bkAssetNode["symbol"] = assetSymbol;
        CompressionType compressionType =
            (assetInfo.compressionFlag != 0) ? CompressionType::BKZIP : CompressionType::None;

        switch (assetType) {
            case BKAssetType::Animation:
                bkAssetNode["type"] = "BK64:ANIM";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Binary:
                bkAssetNode["type"] = "BLOB";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::DemoInput:
                bkAssetNode["type"] = "BK64:DEMO";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Dialog:
                bkAssetNode["type"] = "BK64:DIALOG";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::GruntyQuestion:
                bkAssetNode["type"] = "BK64:GRUNTYQ";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Map:
                bkAssetNode["type"] = "BK64:MAP";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Midi:
                bkAssetNode["type"] = "BLOB";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Model:
                bkAssetNode["type"] = "BK64:MODEL";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::QuizQuestion:
                bkAssetNode["type"] = "BK64:QUIZQ";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Sprite:
                bkAssetNode["type"] = "BK64:SPRITE";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            default:
                // We assigned assetType ourselves, so getting here means a bug.
                throw std::runtime_error("Invalid BKAsset Type Found");
        }
      } catch (const std::exception& e) {
        parseFailures++;
        const auto& bad = assetTableInfo.at(i);
        SPDLOG_ERROR("[BKAssetFactory] skipping slot {} (compFlag={} tFlag={} mode={} offset=0x{:X}): {}", i,
                     bad.compressionFlag, bad.tFlag, bad.assetMode,
                     dataStartRomOffset + bad.offset, e.what());
      }
    }

    if (parseFailures > 0) {
        SPDLOG_WARN("[BKAssetFactory] {} slot(s) failed to parse and were skipped", parseFailures);
    }

    return std::make_shared<BKAssetData>(assetTableInfo, symbolMap);
}

} // namespace BK64
