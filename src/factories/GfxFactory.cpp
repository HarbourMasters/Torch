#include "GfxFactory.h"

#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"
#include <iostream>
#include <string>
#include <fstream>  // for file stream
#include <cstdio>
#include "binarytools/BinaryReader.h"
#include <gfxd.h>

namespace fs = std::filesystem;

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