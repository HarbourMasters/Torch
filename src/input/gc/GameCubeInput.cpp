#include "GameCubeInput.h"
#include "BlockReader.h"
#include "DolFile.h"
#include "Rarc.h"
#include "RelModule.h"

#include "spdlog/spdlog.h"
#include "TinySHA1.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>

namespace GC {

static constexpr uint32_t WINDOW_ALIGN = 0x20;
static constexpr uint32_t SEGMENT_SPAN = 0x1000000;

std::string Sha1Hex(const std::vector<uint8_t>& data) {
    sha1::SHA1 s;
    s.processBytes(data.data(), data.size());
    uint32_t hash[5];
    s.getDigest(hash);
    char buf[41];
    std::snprintf(buf, sizeof(buf), "%08x%08x%08x%08x%08x", hash[0], hash[1], hash[2], hash[3], hash[4]);
    return std::string(buf);
}

static std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string Hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%X", v);
    return buf;
}

static std::string BaseName(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::shared_ptr<GameCubeInput> GameCubeInput::FromPath(const std::filesystem::path& path) {
    if (FolderDisc::Probe(path)) {
        return std::make_shared<GameCubeInput>(std::make_shared<FolderDisc>(path));
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return nullptr;
    }
    auto reader = WrapDiscImage(std::make_shared<RawImageReader>(path));
    if (!GcmDisc::Probe(*reader)) {
        return nullptr;
    }
    return std::make_shared<GameCubeInput>(std::make_shared<GcmDisc>(reader));
}

std::shared_ptr<GameCubeInput> GameCubeInput::FromMemory(std::vector<uint8_t>& data) {
    const bool gcm = data.size() >= GcmDisc::BOOT_SIZE && ReadBE32(data.data() + 0x1C) == GcmDisc::MAGIC;
    const bool ciso = data.size() >= 4 && std::memcmp(data.data(), "CISO", 4) == 0;
    const bool gcz = data.size() >= 4 && data[0] == 0x01 && data[1] == 0xC0 && data[2] == 0x0B && data[3] == 0xB1;
    if (!gcm && !ciso && !gcz) {
        return nullptr;
    }
    auto reader = WrapDiscImage(std::make_shared<MemoryBlockReader>(std::move(data)));
    data.clear();
    if (!GcmDisc::Probe(*reader)) {
        throw std::runtime_error("Compressed image does not contain a GameCube disc");
    }
    return std::make_shared<GameCubeInput>(std::make_shared<GcmDisc>(reader));
}

void GameCubeInput::Open() {
    mDol = mDisc->ReadFile("sys/main.dol");
    mDolHash = Sha1Hex(mDol);
    mVersionCRC = static_cast<uint32_t>(std::stoul(mDolHash.substr(0, 8), nullptr, 16));
    mTitle = mDisc->GetGameId();
    mRevision = mDisc->GetRevision();
}

void GameCubeInput::PlaceDol(YAML::Node node, std::vector<Placement>& out) {
    const auto file = node && node["file"] ? node["file"].as<std::string>() : std::string("sys/main.dol");
    const auto segment = node && node["segment"] ? node["segment"].as<uint32_t>() : 0x80u;
    const auto bytes = file == "sys/main.dol" ? mDol : mDisc->ReadFile(file);

    DolFile dol(bytes);
    const uint32_t base = segment << 24;
    const uint32_t start = dol.GetStart();
    const uint32_t end = dol.GetEnd();
    if (start < base || end > base + SEGMENT_SPAN) {
        throw std::runtime_error("DOL " + file + " does not fit in segment 0x" + Hex(segment));
    }

    Placement p = { segment, 0, std::vector<uint8_t>(end - base, 0), {} };
    dol.Map(p.data.data(), base, end - base);
    out.push_back(std::move(p));
}

void GameCubeInput::PlaceRel(YAML::Node node, std::vector<Placement>& out) {
    const auto file = node["file"].as<std::string>();
    const auto raw = mDisc->ReadFile(file);
    auto decoded = RelModule::Decompress(raw);

    if (node["sha1"]) {
        const auto expected = Lower(node["sha1"].as<std::string>());
        if (Sha1Hex(decoded) == expected) {
            mRelHashSemantics[file] = "decompressed";
        } else if (Sha1Hex(raw) == expected) {
            mRelHashSemantics[file] = "compressed";
        } else {
            throw std::runtime_error("REL " + file + " does not match sha1 " + expected + " (raw " + Sha1Hex(raw) +
                                     ", decompressed " + Sha1Hex(decoded) + ")");
        }
        SPDLOG_CRITICAL("REL {}: sha1 matches the {} file", file, mRelHashSemantics[file]);
    }

    RelModule rel(std::move(decoded));
    const auto names = rel.GetSectionNames();
    std::vector<std::optional<uint32_t>> bases(rel.GetSections().size());

    if (auto sections = node["sections"]) {
        for (auto it = sections.begin(); it != sections.end(); ++it) {
            const auto name = it->first.as<std::string>();
            const auto index = rel.FindSection(name);
            if (!index.has_value()) {
                throw std::runtime_error("REL " + file + " has no section " + name);
            }
            bases[index.value()] = it->second.as<uint32_t>();
        }
    }

    auto data = rel.GetSectionData();
    const auto stats = rel.Link(bases, data);

    SPDLOG_CRITICAL("REL {}: module {} linked, {} relocations applied, {} in unplaced sections, {} to unplaced "
                    "targets, {} to other modules, {} of unknown type",
                    file, rel.GetId(), stats.applied, stats.skippedUnplaced, stats.unresolvedTarget,
                    stats.unknownModule, stats.unknownType);
    for (const auto& [type, count] : stats.byType) {
        SPDLOG_INFO("REL {}: relocation type {}: {}", file, type, count);
    }
    for (const auto& u : stats.unresolved) {
        SPDLOG_WARN("REL {}: type {} at {}+0x{:X} targets unplaced section {} ({}+0x{:X})", file, u.type,
                    names[u.section], u.offset, u.targetSection, names[u.targetSection], u.addend);
    }

    for (size_t i = 0; i < bases.size(); i++) {
        if (!bases[i].has_value()) {
            continue;
        }
        const uint32_t base = bases[i].value();
        SPDLOG_INFO("REL {}: section {} ({}) size 0x{:X} at 0x{:08X}", file, i, names[i], data[i].size(), base);
        if ((base & 0xFFFFFF) + data[i].size() > SEGMENT_SPAN) {
            throw std::runtime_error("REL " + file + " section " + names[i] + " overflows its segment");
        }
        out.push_back({ base >> 24, base & 0xFFFFFF, std::move(data[i]), {} });
    }
}

void GameCubeInput::PlaceFile(YAML::Node node, std::vector<Placement>& out) {
    const auto file = node["file"].as<std::string>();
    const auto segment = node["segment"].as<uint32_t>();
    const auto type = node["type"] ? Lower(node["type"].as<std::string>()) : std::string("raw");
    const auto bytes = mDisc->ReadFile(file);

    if (type == "rarc") {
        Rarc arc(bytes);
        uint32_t offset = 0;
        for (const auto& member : arc.GetMembers()) {
            offset = (offset + WINDOW_ALIGN - 1) & ~(WINDOW_ALIGN - 1);
            Placement p = { segment, offset, member.data,
                            { file + "/" + member.path, BaseName(file) + "/" + member.path } };
            offset += static_cast<uint32_t>(member.data.size());
            out.push_back(std::move(p));
        }
        return;
    }

    if (type != "raw") {
        throw std::runtime_error("Unknown gamecube file type '" + type + "' for " + file);
    }
    out.push_back({ segment, 0, bytes, { file, BaseName(file) } });
}

void GameCubeInput::Assemble(std::vector<Placement>& placements) {
    std::map<uint32_t, std::vector<Placement*>> bySegment;
    for (auto& p : placements) {
        if (p.segment < 0x80 || p.segment > 0xFF) {
            throw std::runtime_error("GameCube windows must use virtual segments 0x80-0xFF");
        }
        bySegment[p.segment].push_back(&p);
    }

    mImage.clear();
    mWindows.clear();
    for (auto& [segment, list] : bySegment) {
        std::sort(list.begin(), list.end(), [](auto* a, auto* b) { return a->offset < b->offset; });
        uint32_t extent = 0;
        for (size_t i = 0; i < list.size(); i++) {
            const uint64_t end = static_cast<uint64_t>(list[i]->offset) + list[i]->data.size();
            if (end > SEGMENT_SPAN) {
                throw std::runtime_error("Segment 0x" + Hex(segment) + " exceeds 16 MiB");
            }
            if (i + 1 < list.size() && end > list[i + 1]->offset) {
                throw std::runtime_error("Overlapping placements in segment 0x" + Hex(segment));
            }
            extent = std::max(extent, static_cast<uint32_t>(end));
        }

        const uint32_t imageOffset = (static_cast<uint32_t>(mImage.size()) + WINDOW_ALIGN - 1) & ~(WINDOW_ALIGN - 1);
        mImage.resize(imageOffset + extent, 0);
        for (auto* p : list) {
            std::copy(p->data.begin(), p->data.end(), mImage.begin() + imageOffset + p->offset);
            for (const auto& name : p->names) {
                mFileOffsets[name] = imageOffset + p->offset;
                mFileAddresses[name] = (segment << 24) | p->offset;
                mFileSizes[name] = static_cast<uint32_t>(p->data.size());
            }
        }
        mVirtualSegments[segment] = imageOffset;
        mWindows.push_back({ segment, imageOffset, extent });
    }
}

void GameCubeInput::Build(YAML::Node& rom) {
    auto gc = rom["gamecube"];
    if (!gc) {
        throw std::runtime_error("config entry " + mDolHash + " has no gamecube block");
    }

    std::vector<Placement> placements;
    PlaceDol(gc["dol"], placements);
    if (auto rels = gc["rel"]) {
        for (size_t i = 0; i < rels.size(); i++) {
            PlaceRel(rels[i], placements);
        }
    }
    if (auto files = gc["files"]) {
        for (size_t i = 0; i < files.size(); i++) {
            PlaceFile(files[i], placements);
        }
    }
    Assemble(placements);
}

void GameCubeInput::LogInfo() {
    SPDLOG_CRITICAL("Game: {}", mTitle);
    SPDLOG_CRITICAL("Revision: {}", mRevision);
    SPDLOG_CRITICAL("Version CRC: 0x{:08X}", mVersionCRC);
    SPDLOG_CRITICAL("Hash: {}", mDolHash);
    for (const auto& w : mWindows) {
        SPDLOG_CRITICAL("Window 0x{:02X}: image 0x{:08X} size 0x{:X}", w.segment, w.imageOffset, w.size);
    }
}

} // namespace GC
