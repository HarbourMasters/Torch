#include "TextureFactory.h"

#include "Companion.h"
#include "utils/MIODecoder.h"

namespace fs = std::filesystem;

static const std::unordered_map <std::string, TextureType> gTextureTypes = {
    { "RGBA16", TextureType::RGBA16bpp },
    { "RGBA32", TextureType::RGBA32bpp },
    { "IA1", TextureType::GrayscaleAlpha4bpp },
	{ "IA4", TextureType::GrayscaleAlpha4bpp },
	{ "IA8", TextureType::GrayscaleAlpha8bpp },
	{ "IA16", TextureType::GrayscaleAlpha16bpp },
};

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
        WRITE_U32(size);
        WRITE_ARRAY(decoded.data() + mio0, size);
    } else {
        WRITE_U32(size);
        WRITE_ARRAY(buffer.data() + offset, size);
    }

	return true;
}
