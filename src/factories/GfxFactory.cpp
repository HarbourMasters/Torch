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
	gfxd_puts("\t");
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

bool GfxFactory::process(LUS::BinaryWriter* writer, YAML::Node& node, std::vector<uint8_t>& buffer) {
    auto offset = node["offset"].as<size_t>();
    auto mio0 = node["mio0"].as<size_t>();

    auto decoded = MIO0Decoder::Decode(buffer, mio0);



	uint64_t buff[] = {0xB800000000000000};
	gfxd_endian(gfxd_endian_big, sizeof(uint64_t));
	gfxd_input_buffer(buff, sizeof(buff));
	
	
	// gfxd_endian(gfxd_endian_host, sizeof(uint32_t));  gfxd_input_buffer(buff, sizeof(buff));


    //gfxd_input_buffer(buff, 64);
    	/* Override the default macro handler to make the output prettier */
	gfxd_macro_fn(macro_fn);

	/* Select F3DEX as the target microcode */
	gfxd_target(gfxd_f3dex);

	/* Set the input endianness to big endian, and the word size to uint64_t */
	//gfxd_endian(gfxd_endian_big, sizeof(uint32_t));

	/* Print an opening brace */
	gfxd_puts("{\n");

	/* Execute until the end of input, or until encountering an invalid
	   command */
	gfxd_execute();

	/* Print a closing brace */
	gfxd_puts("}\n");


    char out[512] = {0};
    gfxd_output_buffer(out, 512);
    
    printf("\n\n");
    std::cout << out;
    printf("\n\n");

    WriteAllText2("gfx.txt", out);

    return true;
}