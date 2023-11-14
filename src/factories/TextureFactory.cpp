#include "TextureFactory.h"
#include "utils/MIODecoder.h"
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
};

uint8_t* alloc_ia8_text_from_i1(uint16_t *in, int16_t width, int16_t height) {
    int32_t inPos;
    uint16_t bitMask;
    int16_t outPos = 0;
    auto out = new uint8_t[width * height];

    for (inPos = 0; inPos < (width * height) / 16; inPos++) {
        bitMask = 0x8000;

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

    return out;
}

void TextureHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto symbol = node["symbol"] ? node["symbol"].as<std::string>() : entryName;
    auto data = std::static_pointer_cast<TextureData>(raw)->mBuffer;

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern u8 " << symbol << "[" << data.size() << "];\n";
}

void TextureCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto data = std::static_pointer_cast<TextureData>(raw)->mBuffer;
    auto symbol = node["symbol"] ? node["symbol"].as<std::string>() : entryName;
    auto format = node["format"].as<std::string>();

    if (format.empty()) {
        printf("ERRROR!!!!");
        return;
    }

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

    write << "ALIGNED8 static const u8 " << symbol << "[] = {\n";
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
    auto format = node["format"].as<std::string>();
    auto width = node["width"].as<uint32_t>();
    auto height = node["height"].as<uint32_t>();
    auto size = node["size"].as<size_t>();
    auto offset = node["offset"].as<size_t>();

	if(!gTextureTypes.contains(format)) {
		return std::nullopt;
	}

	TextureType type = gTextureTypes.at(format);

    std::vector<uint8_t> result;

	if(node["mio0"]){
        auto mio0 = node["mio0"].as<size_t>();
        auto decoded = MIO0Decoder::Decode(buffer, mio0);
        auto data = decoded.data() + offset;
        if(type == TextureType::GrayscaleAlpha1bpp){
            auto ia8 = alloc_ia8_text_from_i1((uint16_t*) data, 8, 16);
            result = std::vector(ia8, ia8 + 8 * 16);
            delete[] ia8;
        }

        result = std::vector(data, data + size);
	} else {
        result = std::vector(buffer.data() + offset, buffer.data() + offset + size);
	}

    SPDLOG_INFO("Texture: {} {}x{} {}", format, width, height, size);
    SPDLOG_INFO("Offset: {}", offset);
    SPDLOG_INFO("Has MIO0: {}", node["mio0"] ? "true" : "false");
    SPDLOG_INFO("Size: {}", result.size());

    if(result.size() == 0){
        return std::nullopt;
    }

    return std::make_shared<TextureData>(type, width, height, result);
}