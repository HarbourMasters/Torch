#include "VerticeFactory.h"

#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"
#include <iostream>
#include <string>
#include <fstream>

namespace fs = std::filesystem;

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

    // Beginning of displaylist
    text += "Vtx "+ name + "[] = {\n";

    for (size_t i = 0; i < numVerts; i++) {
        auto x = str(BSWAP16(vtx[i].v.ob[0]));
        auto y = str(BSWAP16(vtx[i].v.ob[1]));
        auto z = str(BSWAP16(vtx[i].v.ob[2]));

        auto flag = str(BSWAP16(vtx[i].v.flag));
        auto tc1   = str(BSWAP16(vtx[i].v.tc[0]));
        auto tc2   = str(BSWAP16(vtx[i].v.tc[1]));

        auto cn1    = str(vtx[i].v.cn[0]);
        auto cn2    = str(vtx[i].v.cn[1]);
        auto cn3    = str(vtx[i].v.cn[2]);
        auto cn4    = str(vtx[i].v.cn[3]);

        text += "    ";

        // {{{ x, y, z}, f, { t1, t2 }, { c1, c2, c3, c4 }}},
        text += "{{{ "+x+", "+y+", "+z+ "}, "+flag+", { "+tc1+", "+tc2+" }, { 0x"+cn1+ ", "+"0x"+cn2+", 0x"+cn3+", 0x"+cn4+" }}},\n";        
    }

    // End of displaylist
    text += ("};\n");

	WriteAllText("out.txt", text);

    return true;
}
