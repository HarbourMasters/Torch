#include "TextureFactory.h"

#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"

namespace fs = std::filesystem;

static const std::unordered_map <std::string, TextureType> gTextureTypes = {
    { "RGBA16", TextureType::RGBA16bpp },
    { "RGBA32", TextureType::RGBA32bpp },
    { "IA1", TextureType::GrayscaleAlpha1bpp },
	{ "IA4", TextureType::GrayscaleAlpha4bpp },
	{ "IA8", TextureType::GrayscaleAlpha8bpp },
	{ "IA16", TextureType::GrayscaleAlpha16bpp },
};

uint8_t* alloc_ia8_text_from_i1(uint16_t *in, int16_t width, int16_t height) {
    int32_t inPos;
    uint16_t bitMask;
    int16_t outPos = 0;
    uint8_t* out = new uint8_t[width * height];

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

bool TextureFactory::process(LUS::BinaryWriter* writer, YAML::Node& data, std::vector<uint8_t>& buffer) {

    auto format = data["format"].as<std::string>();
    auto width = data["width"].as<uint32_t>();
    auto height = data["height"].as<uint32_t>();
    auto size = data["size"].as<size_t>();
    auto offset = data["offset"].as<size_t>();

	if(!gTextureTypes.contains(format)) {
		return false;
	}

	TextureType type = gTextureTypes.at(format);

    WRITE_HEADER(LUS::ResourceType::Texture, 0);

    WRITE_U32(type);
    WRITE_U32(width);
    WRITE_U32(height);

    if(data["mio0"]){
        auto mio0 = data["mio0"].as<size_t>();
        auto decoded = MIO0Decoder::Decode(buffer, offset);
        auto data = decoded.data() + mio0;
        if(type == TextureType::GrayscaleAlpha1bpp){
            auto ia8 = alloc_ia8_text_from_i1((uint16_t*)data, 8, 16);
            WRITE_U32(8 * 16);
            WRITE_ARRAY(ia8, 8 * 16);
            delete[] ia8;
        } else {
            WRITE_U32(size);
            WRITE_ARRAY(data, size);
        }
    } else {
        WRITE_U32(size);
        WRITE_ARRAY(buffer.data() + offset, size);
    }

    SPDLOG_INFO("Texture: {} {}x{} {}", format, width, height, size);
    SPDLOG_INFO("Offset: {}", offset);
    SPDLOG_INFO("Has MIO0: {}", data["mio0"] ? "true" : "false");

	return true;
}
