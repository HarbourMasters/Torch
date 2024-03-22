#include "MkTexture.h"
#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <iomanip>

extern "C" {
#include "n64graphics/mk64graphics.h"
#include "../BaseFactory.h"
}

static const std::unordered_map <std::string, MkTextureFormat> gTextureFormats = {
    { "RGBA16", { MkTextureType::RGBA16bpp, 16 } },
    { "RGBA32", { MkTextureType::RGBA32bpp, 32 } },
    { "CI4",    { MkTextureType::Palette4bpp, 4 } },
    { "CI8",    { MkTextureType::Palette8bpp, 8 } },
    { "I4",     { MkTextureType::Grayscale4bpp, 4 } },
    { "I8",     { MkTextureType::Grayscale8bpp, 8 } },
    { "IA1",    { MkTextureType::GrayscaleAlpha1bpp, 1 } },
	{ "IA4",    { MkTextureType::GrayscaleAlpha4bpp, 4 } },
	{ "IA8",    { MkTextureType::GrayscaleAlpha8bpp, 8 } },
	{ "IA16",   { MkTextureType::GrayscaleAlpha16bpp, 16 } },
    { "TLUT",   { MkTextureType::TLUT, 16 } },
};

size_t CalculateTextureSize(MkTextureType type, uint32_t width, uint32_t height) {
    switch (type) {
        // 4 bytes per pixel
        case MkTextureType::RGBA32bpp:
            return width * height * 4;
        // 2 bytes per pixel
        case MkTextureType::TLUT:
        case MkTextureType::RGBA16bpp:
        case MkTextureType::GrayscaleAlpha16bpp:
            return width * height * 2;
        // 1 byte per pixel
        case MkTextureType::Grayscale8bpp:
        case MkTextureType::Palette8bpp:
        case MkTextureType::GrayscaleAlpha8bpp:
        // TODO: We need to validate this MegaMech
        case MkTextureType::GrayscaleAlpha1bpp:
            return width * height;
        // 1/2 byte per pixel
        case MkTextureType::Palette4bpp:
        case MkTextureType::Grayscale4bpp:
        case MkTextureType::GrayscaleAlpha4bpp:
            return (width * height) / 2;
        default:
            return 0;
    }
}

std::vector<uint8_t> Mkalloc_ia8_text_from_i1(uint16_t *in, int16_t width, int16_t height) {
    int32_t inPos;
    uint16_t bitMask;
    int16_t outPos = 0;
    const auto out = new uint8_t[width * height];

    for (int32_t inPos = 0; inPos < (width * height) / 16; inPos++) {
        uint16_t bitMask = 0x8000;

        while (bitMask != 0) {
            if (BSWAP16(in[inPos]) & bitMask) {
                out[outPos] = 0xFF;
            } else {
                out[outPos] = 0x00;
            }

            bitMask /= 2;
            outPos++;
        }
    }

    auto result = std::vector(out, out + width * height);
    delete[] out;

    return result;
}

void MkTextureHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<MkTextureData>(raw)->mBuffer;

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    const auto searchTable = Companion::Instance->SearchTable(offset);

    if(searchTable.has_value()){
        const auto [name, start, end, mode] = searchTable.value();

        if(start != offset){
            return;
        }

        write << "extern " << GetSafeNode<std::string>(node, "ctype", "u8") << " " << name << "[][" << data.size() << "];\n";
    } else {
        write << "extern " << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[];\n";
    }
}

void MkTextureCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto texture = std::static_pointer_cast<MkTextureData>(raw);
    auto data = texture->mBuffer;
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto format = GetSafeNode<std::string>(node, "format");

    std::transform(format.begin(), format.end(), format.begin(), tolower);
    (*replacement) += "." + format;

    std::string dpath = Companion::Instance->GetOutputPath() + "/" + (*replacement);
    if(!exists(fs::path(dpath).parent_path())){
        create_directories(fs::path(dpath).parent_path());
    }

    std::ofstream file(dpath + ".inc.c", std::ios::binary);

    size_t byteSize = std::max(1, (int) (texture->mFormat.depth / 8));

    SPDLOG_INFO("Byte Size: {}", byteSize);

    for (int i = 0; i < data.size(); i+=byteSize) {
        if (i % 16 == 0 && i != 0) {
            file << std::endl;
        }

        file << "0x";

        for (int j = 0; j < byteSize; j++) {
            file << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i + j]);
        }

        file << ", ";
    }
    file << std::endl;

    file.close();

    const auto searchTable = Companion::Instance->SearchTable(offset);

    if(searchTable.has_value()){
        const auto [name, start, end, mode] = searchTable.value();

        if(mode != TableMode::Append){
            throw std::runtime_error("Reference mode is not supported for now");
        }

        if(start == offset){
            write << GetSafeNode<std::string>(node, "ctype", "static const u8") << " " << name << "[][" << texture->mBuffer.size() << "] = {\n";
        }

        write << tab << "{\n";
        write << tab << tab << "#include \"" << Companion::Instance->GetOutputPath() + "/" << *replacement << ".inc.c\"\n";
        write << tab << "},\n";

        if(end == offset){
            write << "};\n";
        }
    } else {
        if (Companion::Instance->IsDebug()) {
            if (IS_SEGMENTED(offset)) {
                offset = SEGMENT_OFFSET(offset);
            }
            write << "// 0x" << std::hex << std::uppercase << offset << "\n";
        }
        
        write << GetSafeNode<std::string>(node, "ctype", "static const u8") << " " << symbol  << "[] = {\n";

        write << tab << "#include \"" << Companion::Instance->GetOutputPath() + "/" << *replacement << ".inc.c\"\n";
        write << "};\n";

        if (Companion::Instance->IsDebug()) {
            const auto sz = data.size();

            write << "// size: 0x" << std::hex << std::uppercase << sz << "\n";
            if (IS_SEGMENTED(offset)) {
                offset = SEGMENT_OFFSET(offset);
            }
            write << "// 0x" << std::hex << std::uppercase << (offset + sz) << "\n";
        }

        write << "\n";
    }
}

void MkTextureBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto texture = std::static_pointer_cast<MkTextureData>(raw);
    auto data = texture->mBuffer;

    WriteHeader(writer, LUS::ResourceType::Texture, 0);

    writer.Write((uint32_t) texture->mFormat.type);
    writer.Write(texture->mWidth);
    writer.Write(texture->mHeight);

    writer.Write((uint32_t) data.size());
    writer.Write((char*) data.data(), data.size());
    writer.Finish(write);
}

void MkTextureModdingExporter::Export(std::ostream&write, std::shared_ptr<IParsedData> data, std::string&entryName, YAML::Node&node, std::string* replacement) {
    auto texture = std::static_pointer_cast<MkTextureData>(data);
    auto format = texture->mFormat;
    uint8_t* raw = new uint8_t[CalculateTextureSize(format.type, texture->mWidth, texture->mHeight) * 2];
    int size = 0;

    auto ext = GetSafeNode<std::string>(node, "format");

    std::transform(ext.begin(), ext.end(), ext.begin(), tolower);
    (*replacement) += "." + ext + ".png";

    switch (format.type) {
        case MkTextureType::TLUT:
        case MkTextureType::RGBA16bpp:
        case MkTextureType::RGBA32bpp: {
            rgba* imgr = mkraw2rgba(texture->mBuffer.data(), texture->mWidth, texture->mHeight, format.depth);
            if(mkrgba2png(&raw, &size, imgr, texture->mWidth, texture->mHeight)) {
                throw std::runtime_error("Failed to convert texture to PNG");
            }
            break;
        }
        case MkTextureType::GrayscaleAlpha16bpp:
        case MkTextureType::GrayscaleAlpha8bpp:
        case MkTextureType::GrayscaleAlpha4bpp:
        case MkTextureType::GrayscaleAlpha1bpp: {
            ia* imgia = mkraw2ia(texture->mBuffer.data(), texture->mWidth, texture->mHeight, format.depth);
            if(mkia2png(&raw, &size, imgia, texture->mWidth, texture->mHeight)) {
                throw std::runtime_error("Failed to convert texture to PNG");
            }
            break;
        }
        // case MkTextureType::Palette8bpp:
        // case MkTextureType::Palette4bpp: {
        //     ci* imgi = raw2ci_torch(texture->mBuffer.data(), texture->mWidth, texture->mHeight, format.depth);
        //     if(ci2png(&raw, &size, imgi, texture->mWidth, texture->mHeight)) {
        //         throw std::runtime_error("Failed to convert texture to PNG");
        //     }
        //     break;
        // }
        case MkTextureType::Grayscale8bpp:
        case MkTextureType::Grayscale4bpp: {
            ia* imgi = mkraw2i(texture->mBuffer.data(), texture->mWidth, texture->mHeight, format.depth);
            if(mkia2png(&raw, &size, imgi, texture->mWidth, texture->mHeight)) {
                throw std::runtime_error("Failed to convert texture to PNG");
            }
            break;
        }
        default: {
            SPDLOG_ERROR("Unsupported texture format for modding: {}", ext);
        }
    }

    write.write(reinterpret_cast<char*>(raw), size);
}


std::optional<std::shared_ptr<IParsedData>> MkTextureFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto format = GetSafeNode<std::string>(node, "format");
    uint32_t width;
    uint32_t height;
    uint32_t size;
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    if (format.empty()) {
        SPDLOG_ERROR("Texture entry at {:X} in yaml missing format node\n\
                      Please add one of the following formats\n\
                      rgba16, rgba32, ia16, ia8, ia4, i8, i4, ci8, ci4, 1bpp, tlut", offset);
        return std::nullopt;
    }

	if(!gTextureFormats.contains(format)) {
		return std::nullopt;
	}

	MkTextureFormat fmt = gTextureFormats.at(format);

    if(fmt.type == MkTextureType::TLUT){
        width = GetSafeNode<uint32_t>(node, "colors");
        height = 1;
    } else {
        width = GetSafeNode<uint32_t>(node, "width");
        height = GetSafeNode<uint32_t>(node, "height");
    }

    size = GetSafeNode<uint32_t>(node, "size", CalculateTextureSize(gTextureFormats.at(format).type, width, height));
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, size);
    std::vector<uint8_t> result;

    if(fmt.type == MkTextureType::GrayscaleAlpha1bpp){
        result = Mkalloc_ia8_text_from_i1(reinterpret_cast<uint16_t*>(segment.data), 8, 16);
    } else {
        result = std::vector(segment.data, segment.data + segment.size);
    }

    SPDLOG_INFO("Texture: {}", format);
    if(fmt.type == MkTextureType::TLUT){
        SPDLOG_INFO("Colors: {}", width);
    } else {
        SPDLOG_INFO("Width: {}", width);
        SPDLOG_INFO("Height: {}", height);
    }
    SPDLOG_INFO("Size: {}", size);
    SPDLOG_INFO("Offset: 0x{:X}", offset);

    if(result.size() == 0){
        return std::nullopt;
    }

    return std::make_shared<MkTextureData>(fmt, width, height, result);
}

std::optional<std::shared_ptr<IParsedData>> MkTextureFactory::parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto format = GetSafeNode<std::string>(node, "format");
    int width;
    int height;
    uint32_t size;
    auto offset = GetSafeNode<uint32_t>(node, "offset");

    if (format.empty()) {
        SPDLOG_ERROR("Texture entry at {:X} in yaml missing format node\n\
                      Please add one of the following formats\n\
                      rgba16, rgba32, ia16, ia8, ia4, i8, i4, ci8, ci4, 1bpp, tlut", offset);
        return std::nullopt;
    }

	if(!gTextureFormats.contains(format)) {
		return std::nullopt;
	}

	MkTextureFormat fmt = gTextureFormats.at(format);
    if(fmt.type == MkTextureType::TLUT){
        width = GetSafeNode<uint32_t>(node, "colors");
        height = 1;
    } else {
        width = GetSafeNode<uint32_t>(node, "width");
        height = GetSafeNode<uint32_t>(node, "height");
    }

    uint8_t* raw;
    switch (fmt.type) {
        case MkTextureType::TLUT:
        case MkTextureType::RGBA16bpp:
        case MkTextureType::RGBA32bpp: {
            const auto imgr = mkpng2rgba(buffer.data(), buffer.size(), &width, &height);
            size = width * height * fmt.depth / 8;
            raw = new uint8_t[size];
            if(mkrgba2raw(raw, imgr, width, height, fmt.depth) <= 0){
                throw std::runtime_error("Failed to convert PNG to texture");
            }
            break;
        }
        case MkTextureType::GrayscaleAlpha16bpp:
        case MkTextureType::GrayscaleAlpha8bpp:
        case MkTextureType::GrayscaleAlpha4bpp:
        case MkTextureType::GrayscaleAlpha1bpp: {
            const auto imgia = mkpng2ia(buffer.data(), buffer.size(), &width, &height);
            size = width * height * fmt.depth / 8;
            raw = new uint8_t[size];
            if(mkia2raw(raw, imgia, width, height, fmt.depth) <= 0){
                throw std::runtime_error("Failed to convert PNG to texture");
            }
            break;
        }
        // case MkTextureType::Palette8bpp:
        // case MkTextureType::Palette4bpp: {
        //     SPDLOG_WARN("Converting PNG to CI texture is kind of broken, use at your own risk!");
        //     const auto imgi = png2ci(buffer.data(), buffer.size(), &width, &height);
        //     size = width * height * fmt.depth / 8;
        //     raw = new uint8_t[size];
        //     if(ci2raw_torch(raw, imgi, width, height, fmt.depth) <= 0){
        //         throw std::runtime_error("Failed to convert PNG to texture");
        //     }
        //     break;
        // }
        case MkTextureType::Grayscale8bpp:
        case MkTextureType::Grayscale4bpp: {
            const auto imgi = mkpng2ia(buffer.data(), buffer.size(), &width, &height);
            size = width * height * fmt.depth / 8;
            raw = new uint8_t[size];
            if(mki2raw(raw, imgi, width, height, fmt.depth) <= 0){
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
    if(fmt.type == MkTextureType::TLUT){
        SPDLOG_INFO("Colors: {}", width);
    } else {
        SPDLOG_INFO("Width: {}", width);
        SPDLOG_INFO("Height: {}", height);
    }
    SPDLOG_INFO("Size: {}", size);
    SPDLOG_INFO("Offset: 0x{:X}", offset);

    if(result.size() == 0){
        return std::nullopt;
    }

    return std::make_shared<MkTextureData>(fmt, width, height, result);
}
