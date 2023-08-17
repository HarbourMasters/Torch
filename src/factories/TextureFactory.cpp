#include "TextureFactory.h"

#include <iostream>
#include <filesystem>
#include "Companion.h"
#include "utils/MIODecoder.h"

namespace fs = std::filesystem;

enum class TextureType {
	Error,
	RGBA32bpp,
	RGBA16bpp,
	Palette4bpp,
	Palette8bpp,
	Grayscale4bpp,
	Grayscale8bpp,
	GrayscaleAlpha4bpp,
	GrayscaleAlpha8bpp,
	GrayscaleAlpha16bpp,
};

static const std::unordered_map <std::string, TextureType> gTextureTypes = {
    { ".rgba16", TextureType::RGBA16bpp },
    { ".rgba32", TextureType::RGBA32bpp },
    { ".ia1", TextureType::GrayscaleAlpha4bpp },
	{ ".ia4", TextureType::GrayscaleAlpha4bpp },
	{ ".ia8", TextureType::GrayscaleAlpha8bpp },
	{ ".ia16", TextureType::GrayscaleAlpha16bpp },
};

bool TextureFactory::process(LUS::BinaryWriter* writer, nlohmann::json& data, std::vector<uint8_t>& buffer) {
	std::string path = data["path"];
	std::string ext = fs::path(path).extension().string();

	bool isTileTexture = path.find("cake") != std::string::npos || path.find("skyboxes") != std::string::npos;

	if(isTileTexture) {
		return false;
	}

	if(!gTextureTypes.contains(ext)) {
		return false;
	}

    auto metadata = data["offsets"];
	if(!metadata[3].contains("us")){
		return false;
	}

	// Path: [Width, Height, Size, { Country : [Rom Offset, MIO0 Size] }],

	TextureType type = isTileTexture ? TextureType::RGBA32bpp : gTextureTypes.at(ext);

    WRITE_HEADER(LUS::ResourceType::Texture, 0);

    WRITE_U32(type); // Texture Type
    WRITE_U32(metadata[0]); // Width
    WRITE_U32(metadata[1]); // Height
	size_t size = metadata[2];
	auto offsets = metadata[3]["us"];

	auto* texture = new uint8_t[size];

    if(offsets.size() > 1){
        auto mio0 = MIO0Decoder::Decode(buffer, offsets[0]);
        memcpy(texture, mio0.data() + offsets[1], size);
    } else {
        memcpy(texture, buffer.data() + offsets[0], size);
    }

	WRITE_U32(size); // Texture Data Size
	WRITE_ARRAY(texture, size); // Texture Data

	delete[] texture;
	std::cout << "Processed " << path << '\n';
	return true;
}