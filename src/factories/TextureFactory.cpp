#include "TextureFactory.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <iomanip>
#include <regex>

extern "C" {
#include "n64graphics/n64graphics.h"
#include "BaseFactory.h"
}

static bool isTable = false;
static std::vector<std::string> tableEntries;

static const std::unordered_map<std::string, TextureFormat> sTextureFormats = {
    { "RGBA16", { TextureType::RGBA16bpp, 16 } },
    { "RGBA32", { TextureType::RGBA32bpp, 32 } },
    { "CI4", { TextureType::Palette4bpp, 4 } },
    { "CI8", { TextureType::Palette8bpp, 8 } },
    { "I4", { TextureType::Grayscale4bpp, 4 } },
    { "I8", { TextureType::Grayscale8bpp, 8 } },
    { "IA1", { TextureType::GrayscaleAlpha1bpp, 1 } },
    { "IA4", { TextureType::GrayscaleAlpha4bpp, 4 } },
    { "IA8", { TextureType::GrayscaleAlpha8bpp, 8 } },
    { "IA16", { TextureType::GrayscaleAlpha16bpp, 16 } },
    { "TLUT", { TextureType::TLUT, 16 } },
};

ExportResult TextureHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto format = GetSafeNode<std::string>(node, "format");
    auto texture = std::static_pointer_cast<TextureData>(raw);
    auto data = texture->mBuffer;
    auto isOTR = Companion::Instance->IsOTRMode();
    size_t byteSize = std::max(1, (int)(texture->mFormat.depth / 8));

    const auto searchTable = Companion::Instance->SearchTable(offset);

    if (searchTable.has_value()) {
        const auto [name, start, end, mode, index_size] = searchTable.value();
        unsigned int isize = index_size > -1 ? index_size : data.size() / byteSize;

        if (isOTR) {
            write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";

            tableEntries.push_back(symbol);

            if (end == offset) {
                write << "static const char* " << name << "[] = {\n";
                for (auto& entry : tableEntries) {
                    write << tab_t << entry << ",\n";
                }
                write << "};\n\n";
                tableEntries.clear();
            }
        } else {
            write << "extern " << GetSafeNode<std::string>(node, "ctype", "u8") << " " << name << "[][" << isize
                  << "];\n";
        }
    } else {
        if (isOTR) {
            write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
            if (Companion::Instance->AddTextureDefines()) {
                write << "#define _" << symbol << "_WIDTH 0x" << std::hex << texture->mWidth << std::dec << "\n";
                write << "#define _" << symbol << "_HEIGHT 0x" << std::hex << texture->mHeight << std::dec << "\n";
            }
        } else {
            write << "extern " << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[];\n";
            if (Companion::Instance->AddTextureDefines()) {
                write << "#define _" << symbol << "_WIDTH 0x" << std::hex << texture->mWidth << std::dec << "\n";
                write << "#define _" << symbol << "_HEIGHT 0x" << std::hex << texture->mHeight << std::dec << "\n";
            }
        }
    }

    return std::nullopt;
}

ExportResult TextureCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    auto texture = std::static_pointer_cast<TextureData>(raw);
    auto data = texture->mBuffer;
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto format = GetSafeNode<std::string>(node, "format");

    std::transform(format.begin(), format.end(), format.begin(), tolower);
    (*replacement) += "." + format;

    std::string dpath = Companion::Instance->GetOutputPath() + "/" + (*replacement);
    if (!exists(fs::path(dpath).parent_path())) {
        create_directories(fs::path(dpath).parent_path());
    }

    std::ostringstream imgstream;

    size_t byteSize = std::max(1, (int)(texture->mFormat.depth / 8));
    size_t isize = texture->mBuffer.size() / byteSize;

    for (int i = 0; i < data.size(); i += byteSize) {
        if (i % 16 == 0 && i != 0) {
            imgstream << std::endl;
        }

        imgstream << "0x";

        for (int j = 0; j < byteSize; j++) {
            imgstream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i + j]);
        }

        imgstream << ", ";
    }
    imgstream << std::endl;

    if (!Companion::Instance->IsUsingIndividualIncludes()) {
        std::ofstream file(dpath + ".inc.c", std::ios::binary);
        file << imgstream.str();
        file.close();
    }

    const auto searchTable = Companion::Instance->SearchTable(offset);

    if (searchTable.has_value()) {
        const auto [name, start, end, mode, index_size] = searchTable.value();

        if (mode != TableMode::Append) {
            throw std::runtime_error("Reference mode is not supported for now");
        }

        if (index_size > -1) {
            isize = index_size;
        }

        if (start == offset) {
            write << GetSafeNode<std::string>(node, "ctype", "u8") << " " << name << "[][" << isize << "] = {\n";
        }

        write << tab_t << "{\n";
        if (!Companion::Instance->IsUsingIndividualIncludes()) {
            write << tab_t << tab_t << "#include \"" << Companion::Instance->GetDestRelativeOutputPath() + "/"
                  << *replacement << ".inc.c\"\n";
        } else {
            write << imgstream.str();
        }
        write << tab_t << "},\n";

        if (end == offset) {
            write << "};\n";
            if (Companion::Instance->IsDebug()) {
                write << "// size: 0x" << std::hex << std::uppercase << ASSET_PTR((end - start) + isize * byteSize)
                      << "\n";
            }
        }
    } else {
        write << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[] = {\n";

        if (!Companion::Instance->IsUsingIndividualIncludes()) {
            write << tab_t << "#include \"" << Companion::Instance->GetDestRelativeOutputPath() + "/" << *replacement
                  << ".inc.c\"\n";
        } else {
            write << imgstream.str();
        }
        write << "};\n";

        const auto sz = data.size();
        if (Companion::Instance->IsDebug()) {
            write << "// size: 0x" << std::hex << std::uppercase << sz;
        }

        write << "\n";
    }
    return offset + isize * byteSize;
}

ExportResult TextureBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto texture = std::static_pointer_cast<TextureData>(raw);
    auto data = texture->mBuffer;

    WriteHeader(writer, Torch::ResourceType::Texture, 0);

    if (texture->mFormat.type == TextureType::TLUT) {
        texture->mFormat.type = TextureType::RGBA16bpp;
    }

    writer.Write((uint32_t)texture->mFormat.type);
    writer.Write(texture->mWidth);
    writer.Write(texture->mHeight);

    writer.Write((uint32_t)data.size());
    writer.Write((char*)data.data(), data.size());
    writer.Finish(write);
    return std::nullopt;
}

ExportResult TextureModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> data,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto texture = std::static_pointer_cast<TextureData>(data);
    auto format = texture->mFormat;
    uint8_t* raw = new uint8_t[TextureUtils::CalculateTextureSize(format.type, texture->mWidth, texture->mHeight) * 2];
    int size = 0;

    auto ext = GetSafeNode<std::string>(node, "format");

    std::transform(ext.begin(), ext.end(), ext.begin(), tolower);
    *replacement += "." + ext + ".png";

    switch (format.type) {
        case TextureType::TLUT:
        case TextureType::RGBA16bpp:
        case TextureType::RGBA32bpp: {
            rgba* imgr = raw2rgba(texture->mBuffer.data(), texture->mWidth, texture->mHeight, format.depth);
            if (rgba2png(&raw, &size, imgr, texture->mWidth, texture->mHeight)) {
                throw std::runtime_error("Failed to convert texture to PNG");
            }
            break;
        }
        case TextureType::GrayscaleAlpha16bpp:
        case TextureType::GrayscaleAlpha8bpp:
        case TextureType::GrayscaleAlpha4bpp:
        case TextureType::GrayscaleAlpha1bpp: {
            ia* imgia = raw2ia(texture->mBuffer.data(), texture->mWidth, texture->mHeight, format.depth);
            if (ia2png(&raw, &size, imgia, texture->mWidth, texture->mHeight)) {
                throw std::runtime_error("Failed to convert texture to PNG");
            }
            break;
        }
        case TextureType::Palette8bpp:
        case TextureType::Palette4bpp: {
            if (node["tlut_symbol"]) {
                auto tlut = GetSafeNode<std::string>(node, "tlut_symbol");
                auto palette = Companion::Instance->GetParseDataBySymbol(tlut);

                if (palette.has_value()) {
                    auto palTexture = std::static_pointer_cast<TextureData>(palette.value().data.value());
                    convert_raw_to_ci8(&raw, &size, texture->mBuffer.data(), (uint8_t*)palTexture->mBuffer.data(), 0,
                                       texture->mWidth, texture->mHeight, texture->mFormat.depth,
                                       palTexture->mFormat.depth);
                } else {
                    auto symbol = GetSafeNode<std::string>(node, "symbol");
                    throw std::runtime_error("Could not convert ci8 '" + symbol + "' the tlut symbol name " + tlut +
                                             " is probably wrong for tlut_symbol node");
                }
                break;
            }

            if (node["tlut"]) {
                auto tlut = GetSafeNode<uint32_t>(node, "tlut");
                auto palette = Companion::Instance->GetParseDataByAddr(tlut);

                if (palette.has_value()) {
                    auto palTexture = std::static_pointer_cast<TextureData>(palette.value().data.value());
                    convert_raw_to_ci8(&raw, &size, texture->mBuffer.data(), (uint8_t*)palTexture->mBuffer.data(), 0,
                                       texture->mWidth, texture->mHeight, texture->mFormat.depth,
                                       palTexture->mFormat.depth);
                } else {
                    auto symbol = GetSafeNode<std::string>(node, "symbol");
                    throw std::runtime_error("Could not convert ci8 '" + symbol +
                                             "' the address is probably wrong for tlut address node");
                }
                break;
            }
        }
        case TextureType::Grayscale8bpp:
        case TextureType::Grayscale4bpp: {
            ia* imgi = raw2i(texture->mBuffer.data(), texture->mWidth, texture->mHeight, format.depth);
            if (ia2png(&raw, &size, imgi, texture->mWidth, texture->mHeight)) {
                throw std::runtime_error("Failed to convert texture to PNG");
            }
            break;
        }
        default: {
            SPDLOG_ERROR("Unsupported texture format for modding: {}", ext);
        }
    }

    write.write(reinterpret_cast<char*>(raw), size);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> TextureFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto format = GetSafeNode<std::string>(node, "format");
    auto symbol = GetSafeNode<std::string>(node, "symbol");
    uint32_t width;
    uint32_t height;
    uint32_t size;

    std::transform(format.begin(), format.end(), format.begin(), ::toupper);

    if (format.empty()) {
        SPDLOG_ERROR("Texture entry at {:X} in yaml missing format node\n\
                      Please add one of the following formats\n\
                      rgba16, rgba32, ia16, ia8, ia4, i8, i4, ci8, ci4, 1bpp, tlut",
                     offset);
        return std::nullopt;
    }

    if (!Torch::contains(sTextureFormats, format)) {
        return std::nullopt;
    }

    TextureFormat fmt = sTextureFormats.at(format);

    if (fmt.type == TextureType::TLUT) {
        width = GetSafeNode<uint32_t>(node, "colors");
        height = 1;
    } else {
        width = GetSafeNode<uint32_t>(node, "width");
        height = GetSafeNode<uint32_t>(node, "height");
    }

    if ((format == "CI4" || format == "CI8") && node["tlut"] && node["colors"]) {
        YAML::Node tlutNode;
        const auto tlutOffset = GetSafeNode<uint32_t>(node, "tlut");
        const auto tlutSymbol = GetSafeNode(node, "tlut_symbol", symbol + "_tlut");
        std::ostringstream offsetSeg;
        offsetSeg << std::uppercase << std::hex << tlutOffset;
        tlutNode["symbol"] = std::regex_replace(tlutSymbol, std::regex(R"(OFFSET)"), offsetSeg.str());
        tlutNode["type"] = "TEXTURE";
        tlutNode["format"] = "TLUT";
        tlutNode["offset"] = tlutOffset;
        tlutNode["colors"] = GetSafeNode<uint32_t>(node, "colors");
        node["tlut"] = tlutOffset;
        if (node["tlut_ctype"]) {
            tlutNode["ctype"] = GetSafeNode<std::string>(node, "tlut_ctype");
        }
        Companion::Instance->AddAsset(tlutNode);
    }
    size = GetSafeNode<uint32_t>(node, "size",
                                 TextureUtils::CalculateTextureSize(sTextureFormats.at(format).type, width, height));
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, size);
    std::vector<uint8_t> result;

    if (fmt.type == TextureType::GrayscaleAlpha1bpp) {
        result = TextureUtils::alloc_ia8_text_from_i1(reinterpret_cast<uint16_t*>(segment.data), 8, 16);
    } else {
        result = std::vector(segment.data, segment.data + segment.size);
    }

    SPDLOG_INFO("Texture: {}", format);
    if (fmt.type == TextureType::TLUT) {
        SPDLOG_INFO("Colors: {}", width);
    } else {
        SPDLOG_INFO("Width: {}", width);
        SPDLOG_INFO("Height: {}", height);
    }
    SPDLOG_INFO("Size: {}", size);
    SPDLOG_INFO("Offset: 0x{:X}", offset);

    if (result.size() == 0) {
        return std::nullopt;
    }

    if (result.size() == 0) {
        return std::nullopt;
    }

    return std::make_shared<TextureData>(fmt, width, height, result);
}

std::optional<std::shared_ptr<IParsedData>> TextureFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                          YAML::Node& node) {
    auto format = GetSafeNode<std::string>(node, "format");
    int width;
    int height;
    uint32_t size;
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    if (format.empty()) {
        SPDLOG_ERROR("Texture entry at {:X} in yaml missing format node\n\
                      Please add one of the following formats\n\
                      rgba16, rgba32, ia16, ia8, ia4, i8, i4, ci8, ci4, 1bpp, tlut",
                     offset);
        return std::nullopt;
    }

    if (!Torch::contains(sTextureFormats, format)) {
        return std::nullopt;
    }

    TextureFormat fmt = sTextureFormats.at(format);
    if (fmt.type == TextureType::TLUT) {
        width = GetSafeNode<uint32_t>(node, "colors");
        height = 1;
    } else {
        width = GetSafeNode<uint32_t>(node, "width");
        height = GetSafeNode<uint32_t>(node, "height");
    }

    uint8_t* raw;
    switch (fmt.type) {
        case TextureType::TLUT:
        case TextureType::RGBA16bpp:
        case TextureType::RGBA32bpp: {
            const auto imgr = png2rgba(buffer.data(), buffer.size(), &width, &height);
            size = width * height * fmt.depth / 8;
            raw = new uint8_t[size];
            if (rgba2raw(raw, imgr, width, height, fmt.depth) <= 0) {
                throw std::runtime_error("Failed to convert PNG to texture");
            }
            break;
        }
        case TextureType::GrayscaleAlpha16bpp:
        case TextureType::GrayscaleAlpha8bpp:
        case TextureType::GrayscaleAlpha4bpp:
        case TextureType::GrayscaleAlpha1bpp: {
            const auto imgia = png2ia(buffer.data(), buffer.size(), &width, &height);
            size = width * height * fmt.depth / 8;
            raw = new uint8_t[size];
            if (ia2raw(raw, imgia, width, height, fmt.depth) <= 0) {
                throw std::runtime_error("Failed to convert PNG to texture");
            }
            break;
        }
        case TextureType::Palette8bpp:
        case TextureType::Palette4bpp: {
            // Re-index the edited PNG against this texture's palette to rebuild the
            // CI4/CI8 data.
            rgba* img = png2rgba(buffer.data(), buffer.size(), &width, &height);
            if (img == nullptr) {
                throw std::runtime_error("Failed to read PNG for CI texture");
            }
            rgba* pal = nullptr;
            int palColors = (fmt.depth == 4) ? 16 : 256;
            if (node["tlut_symbol"]) {
                const auto tlut = GetSafeNode<std::string>(node, "tlut_symbol");
                if (auto palette = Companion::Instance->GetParseDataBySymbol(tlut); palette.has_value()) {
                    auto palTex = std::static_pointer_cast<TextureData>(palette.value().data.value());
                    palColors = static_cast<int>(palTex->mWidth);
                    pal = raw2rgba(palTex->mBuffer.data(), palColors, 1, palTex->mFormat.depth);
                }
            }
            if (pal == nullptr) {
                SPDLOG_ERROR("CI texture '{}': could not resolve palette '{}' for re-encode",
                             GetSafeNode<std::string>(node, "symbol", ""),
                             GetSafeNode<std::string>(node, "tlut_symbol", ""));
                return std::nullopt;
            }
            size = width * height * fmt.depth / 8;
            raw = new uint8_t[size];
            if (imgpal2rawci(raw, img, pal, nullptr, size, fmt.depth, width * height, palColors) <= 0) {
                throw std::runtime_error("Failed to convert PNG to CI texture");
            }
            break;
        }
        case TextureType::Grayscale8bpp:
        case TextureType::Grayscale4bpp: {
            const auto imgi = png2ia(buffer.data(), buffer.size(), &width, &height);
            size = width * height * fmt.depth / 8;
            raw = new uint8_t[size];
            if (i2raw(raw, imgi, width, height, fmt.depth) <= 0) {
                throw std::runtime_error("Failed to convert PNG to texture");
            }
            break;
        }
        default: {
            SPDLOG_ERROR("Unsupported texture format for modding: {}", format);
            return std::nullopt;
        }
    }

    auto result = std::vector(raw, raw + size);

    SPDLOG_INFO("Texture: {}", format);
    if (fmt.type == TextureType::TLUT) {
        SPDLOG_INFO("Colors: {}", width);
    } else {
        SPDLOG_INFO("Width: {}", width);
        SPDLOG_INFO("Height: {}", height);
    }
    SPDLOG_INFO("Size: {}", size);
    SPDLOG_INFO("Offset: 0x{:X}", offset);

    if (result.size() == 0) {
        return std::nullopt;
    }

    return std::make_shared<TextureData>(fmt, width, height, result);
}

#ifdef BUILD_UI
#include <array>
#include <fstream>
#include <unordered_map>
#include "ui/BaseBackend.h"
#include "ui/ExportUtils.h"

// symbol -> handle, so each texture is decoded and uploaded only once. The
// backend owns the GPU resources and frees them on shutdown.
static std::unordered_map<std::string, UI::TextureHandle> sTextureCache = {};

static rgba* DecodeToRGBA(const std::shared_ptr<TextureData>& texture, YAML::Node& node) {
    const auto format = texture->mFormat;
    const int w = (int)texture->mWidth;
    const int h = (int)texture->mHeight;
    uint8_t* raw = texture->mBuffer.data();

    switch (format.type) {
        case TextureType::TLUT:
        case TextureType::RGBA16bpp:
        case TextureType::RGBA32bpp:
            return raw2rgba(raw, w, h, format.depth);
        case TextureType::GrayscaleAlpha16bpp:
        case TextureType::GrayscaleAlpha8bpp:
        case TextureType::GrayscaleAlpha4bpp:
        case TextureType::GrayscaleAlpha1bpp: {
            ia* img = raw2ia(raw, w, h, format.depth);
            if (img == nullptr) {
                return nullptr;
            }
            auto* out = (rgba*)malloc(sizeof(rgba) * w * h);
            for (int i = 0; i < w * h; i++) {
                out[i] = { img[i].intensity, img[i].intensity, img[i].intensity, img[i].alpha };
            }
            free(img);
            return out;
        }
        case TextureType::Grayscale8bpp:
        case TextureType::Grayscale4bpp: {
            ia* img = raw2i(raw, w, h, format.depth);
            if (img == nullptr) {
                return nullptr;
            }
            auto* out = (rgba*)malloc(sizeof(rgba) * w * h);
            for (int i = 0; i < w * h; i++) {
                out[i] = { img[i].intensity, img[i].intensity, img[i].intensity, img[i].alpha };
            }
            free(img);
            return out;
        }
        case TextureType::Palette8bpp:
        case TextureType::Palette4bpp: {
            std::optional<ParseResultData> palette;
            if (node["tlut_symbol"]) {
                palette = Companion::Instance->GetParseDataBySymbol(GetSafeNode<std::string>(node, "tlut_symbol"));
            } else if (node["tlut"]) {
                palette = Companion::Instance->GetParseDataByAddr(GetSafeNode<uint32_t>(node, "tlut"));
            }
            if (!palette.has_value() || !palette->data.has_value()) {
                return nullptr;
            }
            auto palTexture = std::static_pointer_cast<TextureData>(palette->data.value());
            // CI + palette -> RGBA16 raw, then RGBA16 -> intermediate RGBA.
            uint8_t* rgba16 = ci2raw(raw, palTexture->mBuffer.data(), w, h, format.depth);
            if (rgba16 == nullptr) {
                return nullptr;
            }
            rgba* out = raw2rgba(rgba16, w, h, 16);
            free(rgba16);
            return out;
        }
        default:
            return nullptr;
    }
}

static UI::TextureHandle GetOrLoadTexture(const ParseResultData& item) {
    auto& node = const_cast<YAML::Node&>(item.node);
    const auto symbol = GetSafeNode<std::string>(node, "symbol", item.name);

    const auto cached = sTextureCache.find(symbol);
    if (cached != sTextureCache.end()) {
        return cached->second;
    }

    auto texture = std::static_pointer_cast<TextureData>(item.data.value());
    rgba* pixels = DecodeToRGBA(texture, node);

    UI::TextureHandle handle = UI::kInvalidTexture;
    if (pixels != nullptr) {
        // Upload is synchronous, so the buffer can be freed right after. Caching
        // a failed kInvalidTexture avoids retrying an undecodable texture.
        handle = UI::GetBackend()->UploadRGBA8(reinterpret_cast<const uint8_t*>(pixels), (int)texture->mWidth,
                                               (int)texture->mHeight);
        free(pixels);
    }

    sTextureCache[symbol] = handle;
    return handle;
}

// Checkerboard backdrop so texture transparency reads against the panel.
static void DrawCheckerboard(ImDrawList* dl, const ImVec2& min, const ImVec2& size) {
    constexpr float kCell = 8.0f;
    const ImU32 a = IM_COL32(48, 48, 52, 255);
    const ImU32 b = IM_COL32(64, 64, 70, 255);
    dl->AddRectFilled(min, min + size, a);
    for (float y = 0; y < size.y; y += kCell) {
        for (float x = 0; x < size.x; x += kCell) {
            if ((int)((x + y) / kCell) % 2 == 0) {
                continue;
            }
            const ImVec2 c0 = min + ImVec2(x, y);
            const ImVec2 c1 = c0 + ImVec2(std::min(kCell, size.x - x), std::min(kCell, size.y - y));
            dl->AddRectFilled(c0, c1, b);
        }
    }
}

float TextureFactoryUI::GetItemHeight(const ParseResultData& item) {
    return 138.0f;
}

void TextureFactoryUI::DrawUI(const ParseResultData& item) {
    auto& node = const_cast<YAML::Node&>(item.node);
    auto texture = std::static_pointer_cast<TextureData>(item.data.value());
    const auto format = GetSafeNode<std::string>(node, "format");
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const UI::TextureHandle handle = GetOrLoadTexture(item);

    constexpr float kThumb = 128.0f;
    const ImVec2 thumb(kThumb, kThumb);

    ImGui::PushID(item.name.c_str());
    ImGui::BeginGroup();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    DrawCheckerboard(dl, origin, thumb);

    if (handle != UI::kInvalidTexture) {
        const float aspect = (float)texture->mWidth / (float)texture->mHeight;
        ImVec2 fit = thumb;
        if (aspect >= 1.0f) {
            fit.y = kThumb / aspect;
        } else {
            fit.x = kThumb * aspect;
        }
        const ImVec2 pad((kThumb - fit.x) * 0.5f, (kThumb - fit.y) * 0.5f);
        ImGui::SetCursorScreenPos(origin + pad);
        ImGui::Image(handle, fit);

        // Hover -> larger preview tooltip.
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            constexpr float kBig = 256.0f;
            ImVec2 big(kBig, kBig);
            if (aspect >= 1.0f) {
                big.y = kBig / aspect;
            } else {
                big.x = kBig * aspect;
            }
            ImGui::Image(handle, big);
            ImGui::Text("%u x %u  %s", texture->mWidth, texture->mHeight, format.c_str());
            ImGui::EndTooltip();
        }
    } else {
        const ImVec2 center = origin + thumb * 0.5f;
        const char* label = "No Preview";
        dl->AddText(center - ImGui::CalcTextSize(label) * 0.5f, IM_COL32(150, 150, 150, 255), label);
    }
    dl->AddRect(origin, origin + thumb, IM_COL32(255, 255, 255, 40), 4.0f);

    // Reserve the full box so SameLine aligns the metadata to its edge.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(thumb);
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextUnformatted(item.name.c_str());
    ImGui::Separator();
    ImGui::Text("Format      %s (%d bpp)", format.c_str(), texture->mFormat.depth);
    ImGui::Text("Resolution  %u x %u", texture->mWidth, texture->mHeight);
    ImGui::Text("Size        %zu bytes", texture->mBuffer.size());
    ImGui::Text("Offset      %s", Torch::to_hex(offset).c_str());
    ImGui::Spacing();
    if (ImGui::SmallButton("Copy symbol")) {
        ImGui::SetClipboardText(item.name.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Export PNG")) {
        const auto path = UI::ExportFilePath(item.name, "png");
        std::string result = path.string();
        try {
            const auto factory = Companion::Instance->GetFactory(item.type);
            const auto exporter =
                factory.has_value() ? factory.value()->GetExporter(ExportType::Modding) : std::nullopt;
            if (!exporter.has_value()) {
                result = "no png exporter for this type";
            } else {
                std::ofstream file(path, std::ios::binary);
                std::string entryName = item.name;
                std::string replacement = item.name;
                YAML::Node copy = YAML::Clone(item.node);
                exporter.value()->Export(file, item.data.value(), entryName, copy, &replacement);
            }
        } catch (const std::exception& e) { result = std::string("export failed: ") + e.what(); }
        UI::NoteExport(item.name, result);
    }
    UI::DrawExportMarker(item.name);
    ImGui::EndGroup();

    ImGui::PopID();
}
#endif
