#include "TextureFactory.h"
#include "utils/Decompressor.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <iomanip>

static const std::unordered_map <std::string, TextureType> gTextureTypes = {
    { "RGBA16", TextureType::RGBA16bpp },
    { "RGBA32", TextureType::RGBA32bpp },
    { "CI4", TextureType::Palette4bpp },
    { "CI8", TextureType::Palette8bpp },
    { "I4", TextureType::Grayscale4bpp },
    { "I8", TextureType::Grayscale8bpp },
    { "IA1", TextureType::GrayscaleAlpha1bpp },
	{ "IA4", TextureType::GrayscaleAlpha4bpp },
	{ "IA8", TextureType::GrayscaleAlpha8bpp },
	{ "IA16", TextureType::GrayscaleAlpha16bpp },
    { "TLUT", TextureType::TLUT },
};

size_t CalculateTextureSize(TextureType type, uint32_t width, uint32_t height) {
    switch (type) {
        // 4 bytes per pixel
        case TextureType::RGBA32bpp:
            return width * height * 4;
        // 2 bytes per pixel
        case TextureType::TLUT:
        case TextureType::RGBA16bpp:
        case TextureType::GrayscaleAlpha16bpp:
            return width * height * 2;
        // 1 byte per pixel
        case TextureType::Grayscale8bpp:
        case TextureType::Palette8bpp:
        case TextureType::GrayscaleAlpha8bpp:
        // TODO: We need to validate this MegaMech
        case TextureType::GrayscaleAlpha1bpp:
            return width * height;
        // 1/2 byte per pixel
        case TextureType::Palette4bpp:
        case TextureType::Grayscale4bpp:
        case TextureType::GrayscaleAlpha4bpp:
            return (width * height) / 2;
        default:
            return 0;
    }
}

std::vector<uint8_t> alloc_ia8_text_from_i1(uint16_t *in, int16_t width, int16_t height) {
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

void TextureHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto data = std::static_pointer_cast<TextureData>(raw)->mBuffer;

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern u8 " << symbol << "[" << data.size() << "];\n";
}

void TextureCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto data = std::static_pointer_cast<TextureData>(raw)->mBuffer;
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto format = GetSafeNode<std::string>(node, "format");

    std::transform(format.begin(), format.end(), format.begin(), tolower);
    (*replacement) += "." + format;

    std::string dpath = Companion::Instance->GetOutputPath() + "/" + (*replacement);
    if(!exists(fs::path(dpath).parent_path())){
        create_directories(fs::path(dpath).parent_path());
    }

    std::ofstream file(dpath + ".inc.c", std::ios::binary);

    for (int i = 0; i < data.size(); i++) {
        if (i % 16 == 0 && i != 0) {
            file << std::endl;
        }

        file << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << ", ";
    }

    file.close();

    if (Companion::Instance->GetGBIMinorVersion() == GBIMinorVersion::Mk64) {
        write << "u8 " << symbol << "[] = {\n";
    } else {
        if(node["ctype"]) {
            write << node["ctype"].as<std::string>() << " " << symbol << "[] = {\n";
        } else {
            write << "static const u8 " << symbol << "[] = {\n";
        }
    }
    write << tab << "#include \"" << Companion::Instance->GetOutputPath() + "/" << (*replacement) << ".inc.c\"\n";
    write << "};\n\n";
}

void TextureBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto texture = std::static_pointer_cast<TextureData>(raw);
    auto data = texture->mBuffer;

    WriteHeader(writer, LUS::ResourceType::Texture, 0);

    writer.Write((uint32_t) texture->mType);
    writer.Write(texture->mWidth);
    writer.Write(texture->mHeight);

    writer.Write((uint32_t) data.size());
    writer.Write((char*) data.data(), data.size());
    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> TextureFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
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

	if(!gTextureTypes.contains(format)) {
		return std::nullopt;
	}

	TextureType type = gTextureTypes.at(format);

    if(type == TextureType::TLUT){
        width = GetSafeNode<uint32_t>(node, "colors");
        height = 1;
    } else {
        width = GetSafeNode<uint32_t>(node, "width");
        height = GetSafeNode<uint32_t>(node, "height");
    }

    size = GetSafeNode<uint32_t>(node, "size", CalculateTextureSize(gTextureTypes.at(format), width, height));
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, size);
    std::vector<uint8_t> result;

    if(type == TextureType::GrayscaleAlpha1bpp){
        result = alloc_ia8_text_from_i1(reinterpret_cast<uint16_t*>(segment.data), 8, 16);
    } else {
        result = std::vector(segment.data, segment.data + segment.size);
    }

    SPDLOG_INFO("Texture: {}", format);
    if(type == TextureType::TLUT){
        SPDLOG_INFO("Colors: {}", width);
    } else {
        SPDLOG_INFO("Width: {}", width);
        SPDLOG_INFO("Height: {}", height);
    }
    SPDLOG_INFO("Size: {}", size);
    SPDLOG_INFO("Offset: 0x{:X}", offset);
    SPDLOG_INFO("Is Compressed: {}", Decompressor::IsCompressed(node) ? "true" : "false");

    if(result.size() == 0){
        return std::nullopt;
    }

    return std::make_shared<TextureData>(type, width, height, result);
}
