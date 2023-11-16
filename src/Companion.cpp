#include "Companion.h"

#include "storm/SWrapper.h"
#include "utils/MIODecoder.h"
#include "factories/sm64/AnimationFactory.h"
#include "factories/sm64/DialogFactory.h"
#include "factories/sm64/DictionaryFactory.h"
#include "factories/sm64/TextFactory.h"
#include "factories/BankFactory.h"
#include "factories/AudioHeaderFactory.h"
#include "factories/SampleFactory.h"
#include "factories/SequenceFactory.h"
#include "factories/VtxFactory.h"
#include "factories/TextureFactory.h"
#include "factories/DisplayListFactory.h"
#include "factories/BlobFactory.h"
#include "factories/LightsFactory.h"
#include "factories/mk64/WaypointFactory.h"
#include "spdlog/spdlog.h"

#include <fstream>
#include <iostream>
#include <filesystem>

using namespace std::chrono;
namespace fs = std::filesystem;

static const std::string regular = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";
static const std::string line = "[%Y-%m-%d %H:%M:%S.%e] [%l] > %v";

void Companion::Init(const ExportType type) {

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    this->gExporterType = type;
    this->RegisterFactory("BLOB", std::make_shared<BlobFactory>());
    this->RegisterFactory("TEXTURE", std::make_shared<TextureFactory>());
    this->RegisterFactory("VTX", std::make_shared<VtxFactory>());
    this->RegisterFactory("LIGHTS", std::make_shared<LightsFactory>());
    this->RegisterFactory("GFX", std::make_shared<DListFactory>());
    this->RegisterFactory("AUDIO:HEADER", std::make_shared<AudioHeaderFactory>());
    this->RegisterFactory("SEQUENCE", std::make_shared<SequenceFactory>());
    this->RegisterFactory("SAMPLE", std::make_shared<SampleFactory>());
    this->RegisterFactory("BANK", std::make_shared<BankFactory>());

    // SM64 specific
    this->RegisterFactory("SM64:DIALOG", std::make_shared<SM64::DialogFactory>());
    this->RegisterFactory("SM64:TEXT", std::make_shared<SM64::TextFactory>());
    this->RegisterFactory("SM64:DICTIONARY", std::make_shared<SM64::DictionaryFactory>());
    this->RegisterFactory("SM64:ANIM", std::make_shared<SM64::AnimationFactory>());
    // this->RegisterFactory("GEO_LAYOUT", new SGeoFactory());

    // MK64 specific
    this->RegisterFactory("MK64:TRACKWAYPOINTS", std::make_shared<MK64::WaypointFactory>());

    this->Process();
}

void Companion::ExtractNode(YAML::Node& node, std::string& name, SWrapper* binary) {
    std::ostringstream stream;

    auto type = node["type"].as<std::string>();
    auto offset = node["offset"].as<uint32_t>();
    std::transform(type.begin(), type.end(), type.begin(), ::toupper);

    SPDLOG_INFO("[{}] Processing {} at 0x{:X}", type, name, offset);
    auto factory = this->GetFactory(type);
    if (!factory.has_value()) {
        SPDLOG_ERROR("No factory found for {}", name);
        return;
    }

    auto result = factory->get()->parse(this->gRomData, node);
    if (!result.has_value()) {
        SPDLOG_ERROR("Failed to process {}", name);
        return;
    }

    auto exporter = factory->get()->GetExporter(this->gExporterType);
    if (!exporter.has_value()) {
        SPDLOG_ERROR("No exporter found for {}", name);
        return;
    }

    for (auto [fst, snd] : this->gAssetDependencies[this->gCurrentFile]) {
        if (snd.second) continue;
        std::string doutput = (this->gCurrentDirectory / fst).string();
        std::replace(doutput.begin(), doutput.end(), '\\', '/');
        this->gAssetDependencies[this->gCurrentFile][fst].second = true;
        this->ExtractNode(snd.first, doutput, binary);
    }

    switch (this->gExporterType) {
    case ExportType::Binary: {
        if (binary == nullptr) break;
        stream.str("");
        stream.clear();
        exporter->get()->Export(stream, result.value(), name, node, &name);
        auto data = stream.str();
        binary->CreateFile(name, std::vector(data.begin(), data.end()));
        break;
    }
    default: {
        exporter->get()->Export(stream, result.value(), name, node, &name);
        break;
    }
    }

    SPDLOG_INFO("Processed {}", name);
    this->gWriteMap[this->gCurrentFile][type].emplace_back(node["offset"].as<uint32_t>(), stream.str());
}

void Companion::Process() {

    if (!fs::exists("config.yml")) {
        SPDLOG_ERROR("No config file found");
        return;
    }

    auto start = duration_cast<milliseconds>(system_clock::now().time_since_epoch());

    std::ifstream input(this->gRomPath, std::ios::binary);
    this->gRomData = std::vector<uint8_t>(std::istreambuf_iterator(input), {});
    input.close();

    this->gCartridge = new N64::Cartridge(this->gRomData);
    gCartridge->Initialize();

    YAML::Node config = YAML::LoadFile("config.yml");

    if (!config[gCartridge->GetHash()]) {
        SPDLOG_ERROR("No config found for {}", gCartridge->GetHash());
        return;
    }

    auto rom = config[gCartridge->GetHash()];
    auto cfg = rom["config"];

    if (!cfg) {
        SPDLOG_ERROR("No config found for {}", gCartridge->GetHash());
        return;
    }

    if (rom["segments"]) {
        this->gSegments = rom["segments"].as<std::vector<uint32_t>>();
    }
    auto path = rom["path"].as<std::string>();
    auto opath = cfg["output"];
    auto gbi = cfg["gbi"];

    switch (gExporterType) {
    case ExportType::Binary: {
        this->gOutputPath = opath && opath["binary"] ? opath["binary"].as<std::string>() : "generic.otr";
        break;
    }
    case ExportType::Header: {
        this->gOutputPath = opath && opath["headers"] ? opath["headers"].as<std::string>() : "headers";
        break;
    }
    case ExportType::Code: {
        this->gOutputPath = opath && opath["code"] ? opath["code"].as<std::string>() : "code";
        break;
    }
    default: {
        SPDLOG_ERROR("Invalid exporter type: {}", gExporterType);
        break;
    }
    }

    if (gbi) {
        auto key = gbi.as<std::string>();

        if (key == "F3D") {
            this->gGBIVersion = GBIVersion::f3d;
        }
        else if (key == "F3DEX") {
            this->gGBIVersion = GBIVersion::f3dex;
        }
        else if (key == "F3DB") {
            this->gGBIVersion = GBIVersion::f3db;
        }
        else if (key == "F3DEX2") {
            this->gGBIVersion = GBIVersion::f3dex2;
        }
        else if (key == "F3DEXB") {
            this->gGBIVersion = GBIVersion::f3dexb;
        }
        else if (key == "F3DEX_MK64") {
            this->gGBIVersion = GBIVersion::f3dex;
            this->gGBIMinorVersion = GBIMinorVersion::Mk64;
        }
        else {
            SPDLOG_ERROR("Invalid GBI version");
            return;
        }
    }

    if (auto sort = cfg["sort"]) {
        if (sort.IsSequence()) {
            this->gWriteOrder = sort.as<std::vector<std::string>>();
        }
        else {
            this->gWriteOrder = sort.as<std::string>();
        }
    }
    else {
        this->gWriteOrder = std::vector<std::string>{
            "LIGHTS", "TEXTURE", "VTX", "GFX"
        };
    }

    if (std::holds_alternative<std::vector<std::string>>(this->gWriteOrder)) {
        for (auto& [key, _] : this->gFactories) {
            auto entries = std::get<std::vector<std::string>>(this->gWriteOrder);

            if (std::find(entries.begin(), entries.end(), key) != entries.end()) {
                continue;
            }

            entries.push_back(key);
        }
    }

    SPDLOG_INFO("------------------------------------------------");
    spdlog::set_pattern(line);

    SPDLOG_INFO("Starting Torch...");
    SPDLOG_INFO("Game: {}", gCartridge->GetGameTitle());
    SPDLOG_INFO("CRC: {}", gCartridge->GetCRC());
    SPDLOG_INFO("Version: {}", gCartridge->GetVersion());
    SPDLOG_INFO("Country: [{}]", gCartridge->GetCountryCode());
    SPDLOG_INFO("Hash: {}", gCartridge->GetHash());
    SPDLOG_INFO("Assets: {}", path);

    auto wrapper = this->gExporterType == ExportType::Binary ? new SWrapper(this->gOutputPath) : nullptr;

    auto vWriter = LUS::BinaryWriter();
    vWriter.SetEndianness(LUS::Endianness::Big);
    vWriter.Write(static_cast<uint8_t>(LUS::Endianness::Big));
    vWriter.Write(gCartridge->GetCRC());

    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (entry.is_directory()) continue;
        YAML::Node root = YAML::LoadFile(entry.path().string());
        this->gCurrentDirectory = relative(entry.path(), path).replace_extension("");
        this->gCurrentFile = entry.path().string();

        for (auto asset = root.begin(); asset != root.end(); ++asset) {
            auto node = asset->second;
            if (!asset->second["offset"]) continue;

            auto output = (this->gCurrentDirectory / asset->first.as<std::string>()).string();
            std::replace(output.begin(), output.end(), '\\', '/');

            this->gAddrMap[this->gCurrentFile][node["offset"].as<uint32_t>()] = std::make_tuple(output, node);
        }

        // Stupid hack because the iteration broke the assets
        root = YAML::LoadFile(entry.path().string());

        for (auto asset = root.begin(); asset != root.end(); ++asset) {

            spdlog::set_pattern(regular);
            SPDLOG_INFO("------------------------------------------------");
            spdlog::set_pattern(line);

            auto entryName = asset->first.as<std::string>();
            std::string output = (this->gCurrentDirectory / entryName).string();
            std::replace(output.begin(), output.end(), '\\', '/');

            this->ExtractNode(asset->second, output, wrapper);
        }

        if (gExporterType != ExportType::Binary) {
            std::string output = (this->gOutputPath / this->gCurrentDirectory).string();

            switch (gExporterType) {
            case ExportType::Header: {
                output += "/definition.h";
                break;
            }
            case ExportType::Code: {
                output += "/root.inc.c";
                break;
            }
            default: break;
            }

            std::ostringstream stream;

            if (std::holds_alternative<std::string>(this->gWriteOrder)) {
                auto sort = std::get<std::string>(this->gWriteOrder);
                std::vector<std::pair<uint32_t, std::string>> outbuf;
                for (const auto& [type, buffer] : this->gWriteMap[this->gCurrentFile]) {
                    outbuf.insert(outbuf.end(), buffer.begin(), buffer.end());
                }
                this->gWriteMap.clear();

                if (sort == "OFFSET") {
                    std::sort(outbuf.begin(), outbuf.end(), [](const auto& a, const auto& b) {
                        return std::get<uint32_t>(a) < std::get<uint32_t>(b);
                    });
                } else if (sort == "ROFFSET") {
                    std::sort(outbuf.begin(), outbuf.end(), [](const auto& a, const auto& b) {
                        return std::get<uint32_t>(a) > std::get<uint32_t>(b);
                    });
                } else if (sort != "LINEAR") {
                    SPDLOG_ERROR("Invalid write order");
                    throw std::runtime_error("Invalid write order");
                }

                for (auto& [symbol, buffer] : outbuf) {
                    stream << buffer;
                }
                outbuf.clear();
            }
            else {
                for (const auto& type : std::get<std::vector<std::string>>(this->gWriteOrder)) {
                    std::vector<std::pair<uint32_t, std::string>> outbuf = this->gWriteMap[this->gCurrentFile][type];

                    std::sort(outbuf.begin(), outbuf.end(), [](const auto& a, const auto& b) {
                        return std::get<uint32_t>(a) > std::get<uint32_t>(b);
                    });

                    for (auto& [symbol, buffer] : outbuf) {
                        stream << buffer;
                    }
                }
            }

            std::string buffer = stream.str();

            if (buffer.empty()) {
                continue;
            }

            std::replace(output.begin(), output.end(), '\\', '/');
            if (!exists(fs::path(output).parent_path())) {
                create_directories(fs::path(output).parent_path());
            }

            std::ofstream file(output, std::ios::binary);

            if (gExporterType == ExportType::Header) {
                std::string symbol = entry.path().stem().string();
                std::transform(symbol.begin(), symbol.end(), symbol.begin(), toupper);
                file << "#ifndef " << symbol << "_H" << std::endl;
                file << "#define " << symbol << "_H" << std::endl << std::endl;
                file << buffer;
                file << std::endl << "#endif" << std::endl;
            }
            else {
                file << buffer;
            }

            file.close();
        }
    }

    spdlog::set_pattern(regular);
    SPDLOG_INFO("------------------------------------------------");
    spdlog::set_pattern(line);
    if (wrapper != nullptr) {
        SPDLOG_INFO("Writing version file");
        wrapper->CreateFile("version", vWriter.ToVector());
        vWriter.Close();
        wrapper->Close();
    }
    auto end = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    SPDLOG_INFO("Done! Took {}ms", end.count() - start.count());
    spdlog::set_pattern(regular);
    SPDLOG_INFO("------------------------------------------------");

    MIO0Decoder::ClearCache();
    this->gCartridge = nullptr;
    Instance = nullptr;
}

void Companion::Pack(const std::string& folder, const std::string& output) {

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    SPDLOG_INFO("------------------------------------------------");

    SPDLOG_INFO("Starting Torch...");
    SPDLOG_INFO("Scanning {}", folder);

    auto start = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    std::unordered_map<std::string, std::vector<char>> files;

    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (entry.is_directory()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        auto data = std::vector(std::istreambuf_iterator(input), {});
        input.close();
        files[entry.path().string()] = data;
    }

    auto wrapper = SWrapper(output);

    for (auto& [path, data] : files) {
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        // Remove parent folder
        normalized = normalized.substr(folder.length() + 1);
        wrapper.CreateFile(normalized, data);
        SPDLOG_INFO("> Added {}", normalized);
    }

    auto end = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    SPDLOG_INFO("Done! Took {}ms", end.count() - start.count());
    SPDLOG_INFO("Exported to {}", output);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    SPDLOG_INFO("------------------------------------------------");

    wrapper.Close();
}

void Companion::RegisterAsset(const std::string& name, YAML::Node& node) {
    if (!node["offset"]) return;
    this->gAssetDependencies[this->gCurrentFile][name] = std::make_pair(node, false);

    auto output = (this->gCurrentDirectory / name).string();
    std::replace(output.begin(), output.end(), '\\', '/');
    this->gAddrMap[this->gCurrentFile][node["offset"].as<uint32_t>()] = std::make_tuple(output, node);
}

void Companion::RegisterFactory(const std::string& type, const std::shared_ptr<BaseFactory>& factory) {
    this->gFactories[type] = factory;
    SPDLOG_DEBUG("Registered factory for {}", type);
}

std::optional<std::shared_ptr<BaseFactory>> Companion::GetFactory(const std::string& type) {
    if (!this->gFactories.contains(type)) {
        SPDLOG_WARN("Factory of type {} not found", type);
        return std::nullopt;
    }

    return this->gFactories[type];
}

std::optional<std::uint32_t> Companion::GetSegmentedAddr(const uint8_t segment) {
    if (segment >= this->gSegments.size()) {
        SPDLOG_DEBUG("Segment {} not found", segment);
        return std::nullopt;
    }

    return this->gSegments[segment];
}

std::optional<std::tuple<std::string, YAML::Node>> Companion::GetNodeByAddr(const uint32_t addr) {
    if (!this->gAddrMap.contains(this->gCurrentFile)) {
        SPDLOG_WARN("Current file {} not found in addr map", this->gCurrentFile, addr);
        return std::nullopt;
    }

    if (!this->gAddrMap[this->gCurrentFile].contains(addr)) {
        SPDLOG_WARN("Current file {} addr {} not found in addr map", this->gCurrentFile, addr);
        return std::nullopt;
    }

    return this->gAddrMap[this->gCurrentFile][addr];
}

std::string Companion::NormalizeAsset(const std::string& name) const {
    auto path = fs::path(this->gCurrentFile).stem().string() + "_" + name;
    return path;
}
