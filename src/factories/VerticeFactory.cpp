#include "VerticeFactory.h"

#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"
#include <iostream>
#include <string>
#include <fstream>  // for file stream

namespace fs = std::filesystem;

typedef short s16;
typedef unsigned short u16;
typedef unsigned char u8;

#define str(value) std::to_string(value)

void WriteAllText(const std::string& filename, const std::string& text) {
    std::ofstream outputFile(filename);

    if (!outputFile.is_open()) {
        std::cerr << "Failed to open the file: " << filename << std::endl;
        return;  // Handle the error as needed
    }

    outputFile << text;
    outputFile.close();
}

bool VerticeFactory::process(LUS::BinaryWriter* writer, YAML::Node& node, std::vector<uint8_t>& buffer) {

    auto offset = node["offset"].as<size_t>();
    auto mio0 = node["mio0"].as<size_t>();
    auto numVerts = node["size"].as<size_t>();
    auto name = node["name"].as<std::string>();

    auto decoded = MIO0Decoder::Decode(buffer, mio0);

    auto *data = decoded.data() + offset;

    Vtx *vtx = reinterpret_cast<Vtx*>(data);

    std::string text = "";

    text += "Vtx "+ name + "[] = {\n";

    for (size_t i = 0; i < numVerts; i++) {
        s16 x = BSWAP16(vtx[i].v.ob[0]);
        s16 y = BSWAP16(vtx[i].v.ob[1]);
        s16 z = BSWAP16(vtx[i].v.ob[2]);

        u16 flag = BSWAP16(vtx[i].v.flag);
        s16 tc1   = BSWAP16(vtx[i].v.tc[0]);
        s16 tc2   = BSWAP16(vtx[i].v.tc[1]);

        u8 cn1    = vtx[i].v.cn[0];
        u8 cn2    = vtx[i].v.cn[1];
        u8 cn3    = vtx[i].v.cn[2];
        u8 cn4    = vtx[i].v.cn[3];

        text += "    {{ " + str(x) + ", " + str(y) + ", " + str(z) + "}, " + str(flag) + ", {" + str(tc1) + ", " + str(tc2) + "}, ";

        text += "{ 0x" + str(cn1) + ", " + "0x" + str(cn2) + ", 0x" + str(cn3) + ", 0x" + str(cn4) + " }}},\n";        
    }

    text += ("};\n");

	WriteAllText("out.txt", text);

    return true;
}
