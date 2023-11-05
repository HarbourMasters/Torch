#include "GfxFactory.h"

#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"
#include <iostream>
#include <string>
#include <fstream>  // for file stream
#include <cstdio>
#include "binarytools/BinaryReader.h"
#include <gfxdis/gfxd.h>

namespace fs = std::filesystem;

typedef short s16;
typedef unsigned short u16;
typedef unsigned char u8;

//#define str(value) std::to_string(value)

// Byte swap a 16-bit unsigned integer


void WriteAllText2(const std::string& filename, const std::string& text) {
    std::ofstream outputFile(filename);

    if (!outputFile.is_open()) {
        std::cerr << "Failed to open the file: " << filename << std::endl;
        return;  // Handle the error as needed
    }

    outputFile << text;
    outputFile.close();
}

static int macro_fn(void)
{
	/* Print a tab before each macro, and a comma and newline after each
	   macro */
	gfxd_puts("    ");
	gfxd_macro_dflt(); /* Execute the default macro handler */
	gfxd_puts(",\n");

	return 0;
}

// static void gfxdis_set_config(void) {
// 	/* Read from stdin and write to stdout */
// 	//gfxd_input_fd(fileno(read()));
// 	//gfxd_output_fd(fileno(write()));

// 	/* Override the default macro handler to make the output prettier */
// 	gfxd_macro_fn(macro_fn);

// 	/* Select F3DEX as the target microcode */
// 	gfxd_target(gfxd_f3dex);

// 	/* Set the input endianness to big endian, and the word size to uint64_t */
// 	gfxd_endian(gfxd_endian_big, sizeof(uint64_t));

// 	/* Print an opening brace */
// 	gfxd_puts("{\n");

// 	/* Execute until the end of input, or until encountering an invalid
// 	   command */
// 	gfxd_execute();

// 	/* Print a closing brace */
// 	gfxd_puts("}\n");
// }

std::vector<uint32_t> reinterpretAsUint32Vector(const std::vector<uint8_t>& decoded, size_t offset) {
    // Check if there's enough data to convert
    if (offset + sizeof(uint32_t) <= decoded.size()) {
        const uint32_t* data = reinterpret_cast<const uint32_t*>(decoded.data() + offset);
        size_t numElements = (decoded.size() - offset) / sizeof(uint32_t);
        return std::vector<uint32_t>(data, data + numElements);
    } else {
        // Handle the case where there's not enough data to convert
        // You might want to return an error or handle this differently
        return std::vector<uint32_t>();
    }
}

bool GfxFactory::process(LUS::BinaryWriter* writer, YAML::Node& node, std::vector<uint8_t>& buffer) {
    char out[5000] = {0};
    auto offset = node["offset"].as<size_t>();
    auto mio0 = node["mio0"].as<size_t>();

    auto decoded = MIO0Decoder::Decode(buffer, mio0);
	uint32_t *data = (uint32_t *) (decoded.data() + offset);

	uintptr_t i = 0;
	// Calculate size
	while((data[i]) != 0x000000B8) {
		printf("Byte %d: 0x%X\n", i, data[i]);
		i += 1;
	}
	printf("count: %d\n\n", i);

	gfxd_input_buffer(data, (i * sizeof(uint32_t)));
    gfxd_output_buffer(out, sizeof(out));

	gfxd_endian(gfxd_endian_big, sizeof(uint32_t));
	gfxd_macro_fn(macro_fn);
	gfxd_target(gfxd_f3dex);

	/* Opening brace */
	gfxd_puts("Gfx myPtr[] = {\n");
	gfxd_execute();
	/* Closing brace */
	gfxd_puts("};\n");
    
    printf("\n\n");
    std::cout << out;
    printf("\n\n");

    WriteAllText2("gfx.txt", out);

    return true;
}