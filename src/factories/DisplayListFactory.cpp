#include "DisplayListFactory.h"
#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"
#include "n64/gbi.h"
#include "Companion.h"
#include <iomanip>
#include <gfxd.h>

#define C0(pos, width) ((w0 >> (pos)) & ((1U << width) - 1))
#define C1(pos, width) ((w1 >> (pos)) & ((1U << width) - 1))

void DListHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    auto symbol = node["symbol"] ? node["symbol"].as<std::string>() : entryName;

    if(Companion::Instance->IsOTRMode()){
        write << "static const Gfx " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n";
        return;
    }

    write << "ALIGNED8 static const Gfx " << symbol << "[] = {\n";
    write << tab << "#include \"" << (*replacement) << ".inc.c\"\n";
    write << "};\n";

}

// gsSP1Quadrangle(0, 1, 2, 3, 0),
void write_gsSP1Quadrangle(void) {
    uint32_t args =     (uint32_t) *( ((uint32_t *) gfxd_macro_data()) + 1 );

    auto v1 = std::to_string( ((args >> 8) & 0xFF) / 2 );
    auto v2 = std::to_string( ((args >> 16) & 0xFF) / 2 );
    auto v3 = std::to_string( (args & 0xFF) / 2 );
    auto v4 = std::to_string( ((args >> 24) & 0xFF) / 2 );
    auto flag = "0";

    auto str = v2 + ", " + v1 + ", " + v3 + ", " + v4 + ", " + flag;

    gfxd_puts(str.c_str());
}


int32_t gfx_format(void) {

    char out[5000] = {0};
    
    gfxd_puts(fourSpaceTab);


    auto macroId = gfxd_macro_id();

    switch(macroId) {
        case gfxd_SPLine3D:
            // mk64
            gfxd_puts("gsSP1Quadrangle(");
            write_gsSP1Quadrangle();
            gfxd_puts(")");
            break;
        default:
            gfxd_macro_dflt();
            break;
    }
    gfxd_puts(",\n");


    return 0;

}

void DListCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto cmds = std::static_pointer_cast<DListData>(raw)->mGfxs;
    auto symbol = node["symbol"].as<std::string>();
    char out[5000] = {0};

    gfxd_input_buffer((uint32_t *) cmds.data(), (int)(sizeof(uint32_t) * cmds.size()));
    gfxd_output_buffer(out, sizeof(out));

    gfxd_endian(gfxd_endian_host, sizeof(uint32_t));
    gfxd_macro_fn(gfx_format);
    gfxd_target(gfxd_f3dex);

    gfxd_puts(("Gfx " + symbol + "[] = {\n").c_str());
    gfxd_execute();
    gfxd_puts("};\n\n");

    write << out;
}

void DListBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto cmds = std::static_pointer_cast<DListData>(raw)->mGfxs;
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::DisplayList, 0);

    while (writer.GetBaseAddress() % 8 != 0)
        writer.Write((uint8_t)0xFF);

    auto bhash = CRC64((*replacement).c_str());
    writer.Write((uint32_t)(G_MARKER << 24));
    writer.Write((uint32_t) 0xBEEFBEEF);
    writer.Write((uint32_t)(bhash >> 32));
    writer.Write((uint32_t)(bhash & 0xFFFFFFFF));

#ifndef _WIN32
#define case case (uint8_t)
#endif

    for(size_t i = 0; i < cmds.size(); i+=2){
        auto w0 = cmds[i];
        auto w1 = cmds[i + 1];
        uint8_t opcode = w0 >> 24;

        switch (opcode) {
            case G_VTX: {
                uint32_t ptr = SEGMENT_OFFSET(w1);
                auto decl = Companion::Instance->GetNodeByAddr(ptr);
                if(decl.has_value()){
                    uint64_t hash = CRC64(std::get<0>(decl.value()).c_str());
                    SPDLOG_INFO("Found vtx: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(decl.value()));

                    // TODO: Find a better way to do this because its going to break on other games
                    Gfx value = { gsSPVertex(C0(10, 6), C0(16, 8) / 2, ptr) };

                    w0 = value.words.w0;
                    w0 &= 0x00FFFFFF;
                    w0 += (G_VTX_OTR_HASH << 24);
                    w1 = value.words.w1;

                    writer.Write(w0);
                    writer.Write(w1);

                    w0 = hash >> 32;
                    w1 = hash & 0xFFFFFFFF;
                } else {
                    SPDLOG_INFO("Warning: Could not find vtx at 0x{:X}", ptr);
                }
                break;
            }
            case G_DL: {
                auto ptr = SEGMENT_OFFSET(w1);
                auto dec = Companion::Instance->GetNodeByAddr(ptr);

                Gfx value = { gsSPDisplayListOTRHash(ptr) };
                w0 = value.words.w0;
                w1 = value.words.w1;

                writer.Write(w0);
                writer.Write(w1);

                if(dec.has_value()){
                    uint64_t hash = CRC64(std::get<0>(dec.value()).c_str());
                    SPDLOG_INFO("Found display list: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(dec.value()));
                    w0 = hash >> 32;
                    w1 = hash & 0xFFFFFFFF;
                } else {
                    w0 = 0;
                    w1 = 0;
                    SPDLOG_INFO("Warning: Could not find display list at 0x{:X}", ptr);
                }
                break;
            }
            case G_SETTIMG: {
                auto ptr = SEGMENT_OFFSET(w1);
                auto dec = Companion::Instance->GetNodeByAddr(ptr);

                Gfx value = { gsDPSetTextureImage(C0(21, 3), C0(19, 2), C0(0, 10), ptr) };
                w0 = value.words.w0 & 0x00FFFFFF;
                w0 += (G_SETTIMG_OTR_HASH << 24);
                w1 = 0;

                writer.Write(w0);
                writer.Write(w1);

                if(dec.has_value()){
                    uint64_t hash = CRC64(std::get<0>(dec.value()).c_str());
                    SPDLOG_INFO("Found texture: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(dec.value()));
                    w0 = hash >> 32;
                    w1 = hash & 0xFFFFFFFF;
                } else {
                    w0 = 0;
                    w1 = 0;
                    SPDLOG_INFO("Warning: Could not find texture at 0x{:X}", ptr);
                }
                break;
            }
            default: break;
        }
        
        writer.Write(w0);
        writer.Write(w1);
    }

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> DListFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto mio0 = node["mio0"].as<uint32_t>();
    auto offset = node["offset"].as<int32_t>();
    auto symbol = node["symbol"].as<std::string>();
    auto decoded = MIO0Decoder::Decode(buffer, mio0);
    auto processing = true;
    LUS::BinaryReader reader(decoded.data(), decoded.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(offset, LUS::SeekOffsetType::Start);
    std::vector<uint32_t> gfxs;

    while (processing){
        auto w0 = reader.ReadUInt32();
        auto w1 = reader.ReadUInt32();

        uint8_t opcode = w0 >> 24;
        //SPDLOG_INFO("W0: 0x{:X}", w0);
        //SPDLOG_INFO("W1: 0x{:X}", w1);
        //SPDLOG_INFO("Opcode: 0x{:X}", opcode);
        gfxs.push_back(w0);
        gfxs.push_back(w1);
        if(opcode == (uint8_t) G_ENDDL) {
            processing = false;
        }
    }

    return std::make_shared<DListData>(gfxs);
}