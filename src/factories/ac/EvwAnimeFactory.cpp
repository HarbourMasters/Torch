#include "EvwAnimeFactory.h"
#include "ACUtils.h"

#include <algorithm>

#define MAX_EVW_ENTRIES 256

namespace AC {

ExportResult EvwAnimeBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto& entries = std::static_pointer_cast<EvwAnimeData>(raw)->mEntries;
    auto writer = LUS::BinaryWriter();
    WriteHeader(writer, Torch::ResourceType::ACTextureAnimation, 0);
    writer.Write((uint32_t)entries.size());
    for (const auto& e : entries) {
        writer.Write(e.segment);
        writer.Write(e.type);
        switch (e.type) {
            case EVW_SCROLL1:
            case EVW_SCROLL2:
                for (size_t i = 0; i < e.scroll.size(); i++) {
                    writer.Write(e.scroll[i]);
                }
                break;
            case EVW_COLREG_MANUAL:
            case EVW_COLREG_LINEAR:
            case EVW_COLREG_NONLINEAR:
                writer.Write(e.frameCount);
                writer.Write(e.keyCount);
                writer.Write((uint16_t)e.keyFrames.size());
                for (const auto k : e.keyFrames) {
                    writer.Write(k);
                }
                writer.Write((uint16_t)(e.prim.size() / 5));
                for (const auto b : e.prim) {
                    writer.Write(b);
                }
                writer.Write((uint16_t)(e.env.size() / 4));
                for (const auto b : e.env) {
                    writer.Write(b);
                }
                break;
            case EVW_TEXANIME:
                writer.Write(e.frameCount);
                writer.Write(e.keyCount);
                writer.Write((uint32_t)e.textures.size());
                for (const auto& t : e.textures) {
                    writer.Write(t);
                }
                WriteBytes(writer, e.pattern);
                writer.Write((uint32_t)e.keyFrames.size());
                for (const auto k : e.keyFrames) {
                    writer.Write(k);
                }
                break;
            default:
                break;
        }
    }
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> EvwAnimeFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto name = NameOf(node);
    std::vector<EvwEntry> entries;

    for (uint32_t i = 0;; i++) {
        if (i >= MAX_EVW_ENTRIES) {
            throw std::runtime_error(name + ": no terminating entry (negative segment) within " +
                                     std::to_string(MAX_EVW_ENTRIES) + " entries");
        }
        Cursor c(ReadBytes(offset + i * 8, 8, name), name);
        EvwEntry e;
        e.segment = c.S8();
        c.Skip(1);
        e.type = c.S16();
        const uint32_t data = c.U32();
        const std::string what = name + " entry " + std::to_string(i);

        switch (e.type) {
            case EVW_SCROLL1:
            case EVW_SCROLL2:
                e.scroll = ReadBytes(data, e.type == EVW_SCROLL1 ? 4 : 8, what + " scroll");
                break;
            case EVW_COLREG_MANUAL:
            case EVW_COLREG_LINEAR:
            case EVW_COLREG_NONLINEAR: {
                Cursor p(ReadBytes(data, 16, what + " colreg"), what);
                e.frameCount = p.U16();
                e.keyCount = p.U16();
                const uint32_t prim = p.U32();
                const uint32_t env = p.U32();
                const uint32_t keys = p.U32();
                const uint32_t colors = e.type == EVW_COLREG_MANUAL ? e.frameCount : e.keyCount;
                if (prim != 0) {
                    e.prim = ReadBytes(prim, colors * 5, what + " prim_colors");
                }
                if (env != 0) {
                    e.env = ReadBytes(env, colors * 4, what + " env_colors");
                }
                if (keys != 0) {
                    Cursor k(ReadBytes(keys, e.keyCount * 2, what + " keyframes"), what);
                    for (uint32_t j = 0; j < e.keyCount; j++) {
                        e.keyFrames.push_back(k.U16());
                    }
                }
                break;
            }
            case EVW_TEXANIME: {
                Cursor p(ReadBytes(data, 16, what + " texanime"), what);
                e.frameCount = p.U16();
                e.keyCount = p.U16();
                const uint32_t table = p.U32();
                const uint32_t pattern = p.U32();
                const uint32_t keys = p.U32();
                e.pattern = ReadBytes(pattern, e.frameCount, what + " animation_pattern");
                uint32_t textures = 0;
                for (const auto idx : e.pattern) {
                    textures = std::max<uint32_t>(textures, idx + 1u);
                }
                Cursor t(ReadBytes(table, textures * 4, what + " texture_tbl"), what);
                for (uint32_t j = 0; j < textures; j++) {
                    e.textures.push_back(AssetPath(t.U32(), "TEXTURE", what + " texture " + std::to_string(j)));
                }
                if (keys != 0) {
                    Cursor k(ReadBytes(keys, e.keyCount * 2, what + " keyframes"), what);
                    for (uint32_t j = 0; j < e.keyCount; j++) {
                        e.keyFrames.push_back(k.U16());
                    }
                }
                break;
            }
            default:
                throw std::runtime_error(what + ": unknown EVW type " + std::to_string(e.type));
        }
        entries.push_back(e);
        if (e.segment < 0 || (i == 0 && e.segment == 0)) {
            break;
        }
    }
    return std::make_shared<EvwAnimeData>(entries);
}

} // namespace AC
