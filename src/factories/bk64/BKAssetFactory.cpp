#include "BKAssetFactory.h"
#include "Companion.h"
#include "ConfigFactory.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"
#include "TinySHA1.hpp"
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

// Pack the language list + optional English->translation string table into the binary
// "langinfo" at the archive root. Layout (LE):
//   u32 version (1)
//   u32 langCount,   per language { u32 dialogIndex, u32 script, u32 nameLen, char name[] }
//   u32 stringCount, per string   { u32 keyLen, char key[], u32 valLen, char val[] }
// script: 0 = latin, 1 = japanese.
static void EmitLangInfo(const std::vector<std::tuple<std::string, uint32_t, uint32_t>>& langs,
                         const std::vector<std::pair<std::string, std::string>>& strings = {}) {
    if (langs.empty()) {
        return;
    }
    std::vector<char> data;
    auto putU32 = [&data](uint32_t v) {
        data.push_back(static_cast<char>(v & 0xFF));
        data.push_back(static_cast<char>((v >> 8) & 0xFF));
        data.push_back(static_cast<char>((v >> 16) & 0xFF));
        data.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    auto putStr = [&data, &putU32](const std::string& s) {
        putU32(static_cast<uint32_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    };
    putU32(1); // format version
    putU32(static_cast<uint32_t>(langs.size()));
    for (const auto& [name, index, script] : langs) {
        putU32(index);
        putU32(script);
        putStr(name);
    }
    putU32(static_cast<uint32_t>(strings.size()));
    for (const auto& [key, val] : strings) {
        putStr(key);
        putStr(val);
    }
    Companion::Instance->RegisterArchiveFile("langinfo", data);
}

// Source the language list (+ string table) for langinfo: a pack's langinfo.yml next
// to modding.yml, else a `langinfo` key on the asset-table node, else (under
// dialog_pack) names defaulted from the cartridge region.
static void RegisterLangInfo(YAML::Node& node) {
    std::vector<std::tuple<std::string, uint32_t, uint32_t>> langs;
    std::vector<std::pair<std::string, std::string>> strings;
    const auto& cfg = Companion::Instance->GetConfig();

    // Prefer a pack-supplied langinfo.yml from the modding source dir; fall back
    // to a langinfo key on the asset-table node.
    YAML::Node langNode;
    YAML::Node stringNode;
    if (cfg.modding && !cfg.moddingPath.empty()) {
        const auto path = fs::path(cfg.moddingPath) / "langinfo.yml";
        if (fs::exists(path)) {
            auto loaded = YAML::LoadFile(path.string());
            langNode = loaded["langinfo"] ? loaded["langinfo"] : loaded;
            stringNode = loaded["strings"];
        }
    }
    if (!langNode && node["langinfo"]) {
        langNode = node["langinfo"];
    }
    if (!stringNode && node["strings"]) {
        stringNode = node["strings"];
    }
    // A localized-string table: { "<English>": "<translation>", ... }. Lets a pack
    // override hardcoded UI strings (world names, parade credits) the asset pipeline
    // can't reach; the runtime falls back to the English key when a string is absent.
    if (stringNode && stringNode.IsMap()) {
        for (const auto& kv : stringNode) {
            strings.emplace_back(kv.first.as<std::string>(), kv.second.as<std::string>());
        }
    }

    if (langNode && langNode.IsSequence()) {
        for (auto entry : langNode) {
            langs.emplace_back(GetSafeNode<std::string>(entry, "name"),
                               GetSafeNode<uint32_t>(entry, "index", 0),
                               GetSafeNode<uint32_t>(entry, "script", 0));
        }
    } else if (cfg.dialogPack) {
        if (auto* cart = Companion::Instance->GetCartridge()) {
            switch (cart->GetCountry()) {
                case N64::CountryCode::Europe:
                    langs = { { "English (UK)", 0, 0 }, { "French", 1, 0 }, { "German", 2, 0 } };
                    break;
                case N64::CountryCode::Japan:
                    langs = { { "Japanese", 0, 1 } };
                    break;
                case N64::CountryCode::NorthAmerica:
                    langs = { { "English (US)", 0, 0 } };
                    break;
                default:
                    break;
            }
        }
    }

    EmitLangInfo(langs, strings);
}

// The pack's internal language folder (assets/lang/<region>/...). A custom pack sets a
// top-level `region` key in langinfo.yml so its paths don't collide with other packs;
// retail packs omit it and fall back to the cartridge region.
static std::string ResolveLangRegion() {
    const auto& cfg = Companion::Instance->GetConfig();
    if (cfg.modding && !cfg.moddingPath.empty()) {
        const auto path = fs::path(cfg.moddingPath) / "langinfo.yml";
        if (fs::exists(path)) {
            auto loaded = YAML::LoadFile(path.string());
            if (loaded["region"]) {
                return loaded["region"].as<std::string>();
            }
        }
    }
    if (auto* cart = Companion::Instance->GetCartridge()) {
        switch (cart->GetCountry()) {
            case N64::CountryCode::Europe:
                return "pal";
            case N64::CountryCode::Japan:
                return "jp";
            case N64::CountryCode::NorthAmerica:
                return "us";
            default:
                break;
        }
    }
    return "pack";
}

std::optional<std::shared_ptr<IParsedData>> BKAssetFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    bool symbolMapExists = false;
    if (node["symbol_map"]) {
        symbolMapExists = true;
    }

    // Emit the pack-level langinfo once, from the asset table: a yaml-declared
    // langinfo key if present, otherwise the cartridge-region fallback under dialogPack.
    RegisterLangInfo(node);

    // Resolve the pack's language folder.
    const std::string langRegion =
        Companion::Instance->GetConfig().dialogPack ? ResolveLangRegion() : std::string();

    // Romhack o2rs overlay the base bk.o2r, whose manifest already lives there: walk the
    // table for side effects (BB config, modified-slot registration) but don't output a
    // partial manifest blob over it.
    if (IsRomhack(buffer)) {
        node["no_export"] = true;
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

    const auto& rom = Companion::Instance->GetRomData();
    const bool romhackMode = IsRomhack(rom);
    size_t skipHits = 0;
    size_t skipMisses = 0;
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

        if (romhackMode && assetSize > 0) {
            const std::string& baselineHash = GetBaselineAssetHash(assetInfo.index);
            if (!baselineHash.empty()) {
                if (assetOffset + assetSize <= rom.size()) {
                    sha1::SHA1 s;
                    s.processBytes(rom.data() + assetOffset, assetSize);
                    uint32_t digest[5];
                    s.getDigest(digest);
                    char buf[41];
                    std::snprintf(buf, sizeof(buf), "%08x%08x%08x%08x%08x", digest[0], digest[1], digest[2], digest[3],
                                  digest[4]);
                    if (baselineHash == buf) {
                        skipHits++;
                        if ((skipHits % 500) == 0) {
                            SPDLOG_WARN("Romhack hash-skip progress: {} hits, {} misses so far", skipHits, skipMisses);
                        }
                        continue;
                    }
                    skipMisses++;
                }
            }
        }

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

        // Dialog-pack mode only wants what actually differs by language: the text
        // assets, plus the assets a pack may redraw for a new script. The font
        // masks and the in-world text/sign models apply to any region, so custom
        // packs (not just the retail JP cart) can carry and replace them.
        if (Companion::Instance->GetConfig().dialogPack) {
            const bool isText = assetType == BKAssetType::Dialog ||
                                assetType == BKAssetType::GruntyQuestion ||
                                assetType == BKAssetType::QuizQuestion;
            // Font masks + text-bearing models / signs / overlays a language pack
            // may relocalize.
            static const std::unordered_set<uint32_t> kLangAssets = {
                0x6EB, // SPRITE_DIALOG_FONT_ALPHAMASK (dialog/quiz/grunty text)
                0x6EC, // SPRITE_BOLD_FONT_LETTERS_ALPHAMASK (world names, headers)
                0x2EE, // ON_VACATIOIN_SIGN
                0x46C, // JIGSAW_PUZZLE
                0x486, // XMAS_TREE_SWITCH
                0x48B, // JIGGY_PODIUM
                0x4EA, // RACE_BANNER_FINISH
                0x4EB, // RACE_BANNER_START
                0x50A, // SHARKFOOD_ISLAND (model with sign)
                0x54C, // GAME OVER
                0x54D, // BANJO_KAZOOIE_SIGN
                0x54E, // COPYRIGHT_OVERLAY
                0x55C, // PRESS_START_OVERLAY
                0x55D, // NO_CONTROLLER_OVERLAY
                0x563, // LEVEL_ENTRY_SIGNS
                0x56C, // THE_END_SIGN
            };
            const uint32_t idx = assetInfo.index;
            bool isLangAsset = kLangAssets.count(idx) != 0;
            // The JP cart additionally carries the kana dialog font and the
            // pre-rendered kana pause-menu world-name banners.
            if (auto* cart = Companion::Instance->GetCartridge();
                cart != nullptr && cart->GetCountry() == N64::CountryCode::Japan) {
                isLangAsset = isLangAsset || idx == 0x6EA || (idx >= 0xE2C && idx <= 0xE38);
            }
            if (!isText && !isLangAsset) {
                continue;
            }
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

        if (Companion::Instance->GetConfig().dialogPack) {
            assetSymbol = "lang/" + langRegion + "/" + assetSymbol;
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

    if (romhackMode) {
        SPDLOG_WARN("Romhack hash-skip: {} slots matched v1.0 baseline (skipped), {} differed (extracted), {} failed",
                    skipHits, skipMisses, parseFailures);
    } else if (parseFailures > 0) {
        SPDLOG_WARN("[BKAssetFactory] {} slot(s) failed to parse and were skipped", parseFailures);
    }

    // Additive lang assets: slots the base ROM leaves empty but a pack fills via an
    // ASSET_<id>_* yaml in modding.yml (e.g. region-specific dialog or 0x1600+ banners).
    // parse_modding builds these from the yaml alone, so the offset below is a placeholder.
    // No-op outside modding import (gModdedAssetPaths is empty on export).
    if (Companion::Instance->GetConfig().dialogPack) {
        size_t additive = 0;
        for (const auto& [name, yamlPath] : Companion::Instance->GetModdedAssetPaths()) {
            auto langPos = name.find("lang/");
            if (langPos == std::string::npos) {
                continue;
            }
            // Only an asset's own yaml is additive; its texture sub-assets (PNGs, e.g. a
            // sprite's chunks) are created by the factory's parse_modding, so skip them.
            if (yamlPath.size() < 5 || yamlPath.compare(yamlPath.size() - 5, 5, ".yaml") != 0) {
                continue;
            }
            const std::string sym = name.substr(langPos); // lang/<region>/<folder>/ASSET_<id>_<rest>
            // Split into [lang, region, folder, stem].
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= sym.size()) {
                size_t slash = sym.find('/', start);
                if (slash == std::string::npos) {
                    parts.emplace_back(sym.substr(start));
                    break;
                }
                parts.emplace_back(sym.substr(start, slash - start));
                start = slash + 1;
            }
            if (parts.size() < 4) {
                continue;
            }
            const std::string& folder = parts[2];
            const char* assetTypeName = nullptr;
            if (folder == "dialog") {
                assetTypeName = "BK64:DIALOG";
            } else if (folder == "quizq") {
                assetTypeName = "BK64:QUIZQ";
            } else if (folder == "gruntyq") {
                assetTypeName = "BK64:GRUNTYQ";
            } else if (folder == "sprite") {
                assetTypeName = "BK64:SPRITE"; // e.g. world-name banners at 0x1600+
            } else {
                continue;
            }
            const std::string& stem = parts[3];
            if (stem.rfind("ASSET_", 0) != 0) {
                continue;
            }
            size_t idStart = 6; // strlen("ASSET_")
            size_t idEnd = stem.find('_', idStart);
            std::string idHex = (idEnd == std::string::npos) ? stem.substr(idStart) : stem.substr(idStart, idEnd - idStart);
            uint32_t id = 0;
            try {
                id = static_cast<uint32_t>(std::stoul(idHex, nullptr, 16));
            } catch (const std::exception&) {
                continue;
            }
            if (symbolMap.count(id)) {
                continue; // the base table already covers this slot
            }
            YAML::Node addNode;
            addNode["offset"] = id; // synthetic; AddSubFileAsset zeroes it, parse_modding ignores it
            addNode["symbol"] = sym;
            addNode["type"] = assetTypeName;
            addNode["additive"] = true; // no ROM asset at this id — build entirely from the yaml
            if (Companion::Instance->AddSubFileAsset(addNode, sym, CompressionType::None, 0)) {
                symbolMap[id] = sym;
                additive++;
                SPDLOG_INFO("[BKAssetFactory] additive lang asset 0x{:X} -> {}", id, sym);
            }
        }
        if (additive > 0) {
            SPDLOG_INFO("[BKAssetFactory] emitted {} additive lang asset(s)", additive);
        }
    }

    // Detect BB romhack configuration from extended ROMs and write binary blob.
    // Derive a short name from the output path.
    std::string romName;
    {
        std::string outPath = Companion::Instance->GetOutputPath();
        auto slash = outPath.find_last_of("/\\");
        std::string filename = (slash != std::string::npos) ? outPath.substr(slash + 1) : outPath;
        auto dot = filename.rfind('.');
        romName = (dot != std::string::npos) ? filename.substr(0, dot) : filename;
    }
    std::vector<char> configBlob = BuildGameConfigBlob(buffer, romName);
    if (!configBlob.empty()) {
        SPDLOG_INFO("BB romhack config detected ({} bytes), writing to o2r as '{}'", configBlob.size(), romName);
        Companion::Instance->RegisterCompanionFile("aGameConfig", configBlob);
    }

    return std::make_shared<BKAssetData>(assetTableInfo, symbolMap);
}

// Route a dialog-pack build to mods/lang/bk<region>.o2r when no explicit binary name is
// given. Inert unless the rom config sets dialog_pack, so it's safe to call for any rom.
void BKAssetFactory::PreprocessConfig(YAML::Node& cfg, N64::Cartridge* cart) {
    if (!cfg || !cfg["dialog_pack"] || !cfg["dialog_pack"].as<bool>()) {
        return;
    }

    std::string name;
    if (cfg["output"] && cfg["output"]["binary"]) {
        name = fs::path(cfg["output"]["binary"].as<std::string>()).filename().string();
    }
    if (name.empty() || name == "bk.o2r") {
        std::string region = "pack";
        if (cart != nullptr) {
            switch (cart->GetCountry()) {
                case N64::CountryCode::Europe:
                    region = "pal";
                    break;
                case N64::CountryCode::Japan:
                    region = "jp";
                    break;
                case N64::CountryCode::NorthAmerica:
                    region = "us";
                    break;
                default:
                    break;
            }
        }
        name = "bk" + region + ".o2r";
    }

    cfg["output"]["binary"] = "mods/lang/" + name;
}

} // namespace BK64
