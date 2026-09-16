#include "CharsetFactory.h"
#include "Companion.h"
#include <unordered_map>
#include "utils/Decompressor.h"
#include "utils/TextureUtils.h"
#include "spdlog/spdlog.h"
#include <sstream>

ExportResult PM64CharsetHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern u8 " << symbol << "[];\n";
    return std::nullopt;
}

static void RegisterTexture(const std::string& name, uint32_t type, uint32_t w, uint32_t h, const uint8_t* px,
                            size_t bytes) {
    auto writer = LUS::BinaryWriter();
    BaseExporter::WriteHeader(writer, Torch::ResourceType::Texture, 0);
    writer.Write(type);
    writer.Write(w);
    writer.Write(h);
    writer.Write((uint32_t)bytes);
    writer.Write((char*)px, bytes);
    std::stringstream ss;
    writer.Finish(ss);
    std::string str = ss.str();
    Companion::Instance->RegisterCompanionFile(name, std::vector<char>(str.begin(), str.end()));
}

static void RegisterCompanionTexture(const std::string& name, uint32_t type, uint32_t w, uint32_t h, const uint8_t* px,
                                     size_t bytes) {
    auto writer = LUS::BinaryWriter();
    BaseExporter::WriteHeader(writer, Torch::ResourceType::Texture, 0);
    writer.Write(type);
    writer.Write(w);
    writer.Write(h);
    writer.Write((uint32_t)bytes);
    writer.Write((char*)px, bytes);
    std::stringstream ss;
    writer.Finish(ss);
    std::string str = ss.str();
    Companion::Instance->RegisterCompanionFile(name, std::vector<char>(str.begin(), str.end()));
}

static void RegisterImageCompanions(YAML::Node& node, const std::string& entryName, const std::vector<uint8_t>& data) {
    std::string base = entryName;
    const auto slash = base.rfind('/');
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    if (node["img_format"]) {
        static const std::unordered_map<std::string, std::pair<TextureType, double>> kFormats = {
            { "RGBA32", { TextureType::RGBA32bpp, 4.0 } },         { "RGBA16", { TextureType::RGBA16bpp, 2.0 } },
            { "CI4", { TextureType::Palette4bpp, 0.5 } },          { "CI8", { TextureType::Palette8bpp, 1.0 } },
            { "I4", { TextureType::Grayscale4bpp, 0.5 } },         { "I8", { TextureType::Grayscale8bpp, 1.0 } },
            { "IA4", { TextureType::GrayscaleAlpha4bpp, 0.5 } },   { "IA8", { TextureType::GrayscaleAlpha8bpp, 1.0 } },
            { "IA16", { TextureType::GrayscaleAlpha16bpp, 2.0 } },
        };
        const auto fmt = GetSafeNode<std::string>(node, "img_format");
        const auto w = GetSafeNode<uint32_t>(node, "img_width");
        const auto h = GetSafeNode<uint32_t>(node, "img_height");
        const auto it = kFormats.find(fmt);
        if (it != kFormats.end()) {
            const size_t bytes = (size_t)(w * h * it->second.second);
            if (bytes <= data.size()) {
                RegisterCompanionTexture(base + "_img", (uint32_t)it->second.first, w, h, data.data(), bytes);
            }
        }
    }
    if (node["tlut_colors"]) {
        const auto colors = GetSafeNode<uint32_t>(node, "tlut_colors");
        std::string img = base;
        for (const char* suffix : { "_palette", "_pal" }) {
            const std::string s = suffix;
            if (img.size() > s.size() && img.compare(img.size() - s.size(), s.size(), s) == 0) {
                img = img.substr(0, img.size() - s.size());
                break;
            }
        }
        if ((size_t)colors * 2 <= data.size()) {
            RegisterCompanionTexture(img + "_img_tlut", (uint32_t)TextureType::RGBA16bpp, colors, 1, data.data(),
                                     (size_t)colors * 2);
        }
    }
}

ExportResult PM64CharsetBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    WriteHeader(writer, Torch::ResourceType::Blob, 0);
    writer.Write((uint32_t)data.size());
    writer.Write((char*)data.data(), data.size());
    writer.Finish(write);

    std::string base = entryName;
    auto lastSlash = base.rfind('/');
    if (lastSlash != std::string::npos) {
        base = base.substr(lastSlash + 1);
    }

    RegisterImageCompanions(node, entryName, data);
    if (node["glyph_width"]) {
        const auto w = GetSafeNode<uint32_t>(node, "glyph_width");
        const auto h = GetSafeNode<uint32_t>(node, "glyph_height");
        const size_t bytes = (size_t)w * h / 2;
        for (size_t i = 0; bytes > 0 && (i + 1) * bytes <= data.size(); i++) {
            RegisterTexture(base + "_" + std::to_string(i), (uint32_t)TextureType::Palette4bpp, w, h,
                            data.data() + i * bytes, bytes);
        }
    } else if (node["palette_stride"]) {
        const auto stride = GetSafeNode<uint32_t>(node, "palette_stride");
        for (size_t i = 0; stride > 0 && i * stride + 32 <= data.size(); i++) {
            RegisterTexture(base + "_" + std::to_string(i), (uint32_t)TextureType::RGBA16bpp, 16, 1,
                            data.data() + i * stride, 32);
        }
    }

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> PM64CharsetFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    return std::make_shared<RawBuffer>(segment.data, segment.size);
}
