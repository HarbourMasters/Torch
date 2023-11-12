#include "DisplayListFactory.h"
#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <gfxd.h>
#include <fstream>

#define C0(pos, width) ((w0 >> (pos)) & ((1U << width) - 1))

template< typename T >
std::string to_hex(T number, bool append0x = true) {
    std::stringstream stream;
    if(append0x) {
        stream << "0x";
    }
    stream << std::setfill ('0') << std::setw(sizeof(T)*2)
           << std::hex << number;
    return stream.str();
}

void override_gsSP1Quadrangle(void) {
    auto *gfx = static_cast<const Gfx*>(gfxd_macro_data());

    auto w1 = gfx->words.w1;

    auto v1 = std::to_string( ((w1 >> 8) & 0xFF) / 2 );
    auto v2 = std::to_string( ((w1 >> 16) & 0xFF) / 2 );
    auto v3 = std::to_string( (w1 & 0xFF) / 2 );
    auto v4 = std::to_string( ((w1 >> 24) & 0xFF) / 2 );
    auto flag = "0";

    const auto str = v2 + ", " + v1 + ", " + v3 + ", " + v4 + ", " + flag;

    gfxd_puts("gsSP1Quadrangle(");
    gfxd_puts(str.c_str());
    gfxd_puts(")");
}

void override_gsSPVertex(void) {
    auto *gfx = static_cast<const Gfx*>(gfxd_macro_data());
    auto w0 = gfx->words.w0;
    auto w1 = gfx->words.w1;

#ifdef F3DEX_GBI_2
    auto nvtx = std::to_string(C0(12, 8));
    auto didx = std::to_string(C0(1, 7) - C0(12, 8));
#elif defined(F3DEX_GBI) || defined(F3DLP_GBI)
    auto nvtx = std::to_string(C0(10, 6));
    auto didx = std::to_string(C0(16, 8) / 2);
#else
    auto nvtx = std::to_string((C0(0, 16)) / sizeof(Vtx));
    auto didx = std::to_string(C0(16, 4));
#endif

    auto out = to_hex(w1);

    uint32_t ptr = SEGMENT_OFFSET(w1);
    if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); decl.has_value()){
        auto node = std::get<1>(decl.value());
        auto symbol = node["symbol"].as<std::string>();
        SPDLOG_INFO("Found vtx: 0x{:X} Symbol: {}", ptr, symbol);
        out = symbol;
    } else {
        SPDLOG_WARN("Warning: Could not find vtx at 0x{:X}", ptr);
    }

    const auto str = out + ", " + nvtx + ", " + didx;

    gfxd_puts("gsSPVertex(");
    gfxd_puts(str.c_str());
    gfxd_puts(")");
}

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

void DListCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto cmds = std::static_pointer_cast<DListData>(raw)->mGfxs;
    const auto symbol = node["symbol"].as<std::string>();
    char out[0xFFFF] = {0};

    gfxd_input_buffer(cmds.data(), sizeof(uint32_t) * cmds.size());
    gfxd_output_buffer(out, sizeof(out));

    gfxd_endian(gfxd_endian_host, sizeof(uint32_t));
    gfxd_macro_fn([] {
        auto macroId = gfxd_macro_id();
        gfxd_puts(fourSpaceTab);

        switch(macroId) {
            // Add this for mk64 only
            case gfxd_SPLine3D:
                override_gsSP1Quadrangle();
                break;
            case gfxd_SPVertex:
                override_gsSPVertex();
                break;
            default:
                gfxd_macro_dflt();
                break;
        }
        gfxd_puts(",\n");
        return 0;
    });
    gfxd_target(gfxd_f3d);

    gfxd_puts(("Gfx " + symbol + "[] = {\n").c_str());
    gfxd_execute();
    gfxd_puts("};\n\n");

    write << std::string(out);
}

void DebugDisplayList(uint32_t w0, uint32_t w1){
    uint32_t dlist[] = {w0, w1};
    gfxd_input_buffer(dlist, sizeof(dlist));
    gfxd_output_fd(fileno(stdout));
    gfxd_endian(gfxd_endian_host, sizeof(uint32_t));
    gfxd_macro_fn([](){
        gfxd_puts("> ");
        gfxd_macro_dflt();
        gfxd_puts("\n");
        return 0;
    });
    gfxd_target(gfxd_f3d);
    gfxd_execute();
    int bp = 0;
}

void DListBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto cmds = std::static_pointer_cast<DListData>(raw)->mGfxs;
    auto writer = LUS::BinaryWriter();

    WriteHeader(writer, LUS::ResourceType::DisplayList, 0);

    while (writer.GetBaseAddress() % 8 != 0)
        writer.Write(static_cast<uint8_t>(0xFF));

    auto bhash = CRC64((*replacement).c_str());
    writer.Write(static_cast<uint32_t>((G_MARKER << 24)));
    writer.Write(0xBEEFBEEF);
    writer.Write(static_cast<uint32_t>(bhash >> 32));
    writer.Write(static_cast<uint32_t>(bhash & 0xFFFFFFFF));

#ifndef _WIN32
#define case case (uint8_t)
#endif

    for(size_t i = 0; i < cmds.size(); i+=2){
        auto w0 = cmds[i];
        auto w1 = cmds[i + 1];
        uint8_t opcode = w0 >> 24;

        switch (opcode) {
            case G_VTX: {
                auto nvtx = (C0(0, 16)) / sizeof(Vtx);
                auto didx = C0(16, 4);
                uint32_t ptr = SEGMENT_OFFSET(w1);

                if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); decl.has_value()){
                    uint64_t hash = CRC64(std::get<0>(decl.value()).c_str());
                    SPDLOG_INFO("Found vtx: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(decl.value()));

                    // TODO: Find a better way to do this because its going to break on other games
                    Gfx value = gsSPVertexOTR(didx, nvtx, 0);

                    SPDLOG_INFO("gsSPVertex({}, {}, 0x{:X})", nvtx, didx, ptr);

                    w0 = value.words.w0;
                    w1 = value.words.w1;

                    writer.Write(w0);
                    writer.Write(w1);

                    w0 = hash >> 32;
                    w1 = hash & 0xFFFFFFFF;
                } else {
                    SPDLOG_WARN("Warning: Could not find vtx at 0x{:X}", ptr);
                }
                break;
            }
            case G_DL: {
                auto ptr = SEGMENT_OFFSET(w1);
                auto dec = Companion::Instance->GetNodeByAddr(ptr);

                Gfx value = gsSPDisplayListOTRHash(ptr);
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
                    SPDLOG_WARN("Warning: Could not find display list at 0x{:X}", ptr);
                }
                break;
            }
            case G_SETTIMG: {
                auto ptr = SEGMENT_OFFSET(w1);
                auto dec = Companion::Instance->GetNodeByAddr(ptr);

                Gfx value = gsDPSetTextureImage(C0(21, 3), C0(19, 2), C0(0, 10), ptr);
                w0 = value.words.w0 & 0x00FFFFFF;
                w0 += (G_SETTIMG_OTR_HASH << 24);
                w1 = value.words.w1;

                writer.Write(w0);
                writer.Write(w1);

                if(dec.has_value()){
                    uint64_t hash = CRC64(std::get<0>(dec.value()).c_str());
                    SPDLOG_INFO("Found texture: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(dec.value()));
                    w0 = hash >> 32;
                    w1 = hash & 0xFFFFFFFF;
                } else {
                    SPDLOG_WARN("Warning: Could not find texture at 0x{:X}", ptr);
                }
                break;
            }
            default: break;
        }

        writer.Write(w0);
        writer.Write(w1);
        DebugDisplayList(w0, w1);

    }

#undef case

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> DListFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto mio0 = node["mio0"].as<uint32_t>();
    const auto offset = node["offset"].as<int32_t>();
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
        gfxs.push_back(w0);
        gfxs.push_back(w1);

        switch (opcode) {
            case static_cast<uint8_t>(G_ENDDL):
                processing = false;
                break;
            case static_cast<uint8_t>(G_DL): {
                auto ptr = SEGMENT_OFFSET(w1);
                auto dec = Companion::Instance->GetNodeByAddr(ptr);

                if(!dec.has_value()){
                    SPDLOG_INFO("Could not find declarated display list at 0x{:X}, trying to autogenerate it", w1);
                    auto addr = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(w1));
                    if(!addr.has_value()) {
                        SPDLOG_WARN("Warning: Could not find segment {}", SEGMENT_NUMBER(w1));
                        break;
                    }

                    auto rom = Companion::Instance->GetRomData();
                    auto factory = Companion::Instance->GetFactory("GFX")->get();
                    std::string output = "dl_" + to_hex(offset + ptr, false);
                    YAML::Node dl;
                    dl["type"] = "GFX";
                    dl["mio0"] = addr.value();
                    dl["offset"] = ptr;
                    dl["symbol"] = output;
                    auto result = factory->parse(rom, dl);

                    if(!result.has_value()){
                        break;
                    }

                    Companion::Instance->RegisterAsset(output, dl);
                } else {
                    SPDLOG_WARN("Warning: Could not find display list at 0x{:X}", ptr);
                }
                break;
            }
            case static_cast<uint8_t>(G_VTX):
#ifdef F3DEX_GBI_2
                auto nvtx = C0(12, 8));
#elif defined(F3DEX_GBI) || defined(F3DLP_GBI)
                auto nvtx = C0(10, 6));
#else
                auto nvtx = (C0(0, 16)) / sizeof(Vtx);
#endif
                uint32_t ptr = SEGMENT_OFFSET(w1);

                if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); !decl.has_value()){
                    SPDLOG_INFO("Could not find declarated vtx at 0x{:X}, trying to autogenerate it", w1);
                    auto addr = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(w1));
                    if(!addr.has_value()) {
                        SPDLOG_WARN("Warning: Could not find segment {}", SEGMENT_NUMBER(w1));
                        break;
                    }

                    auto rom = Companion::Instance->GetRomData();
                    auto factory = Companion::Instance->GetFactory("VTX")->get();
                    std::string output = "vtx_" + to_hex(offset + ptr, false);
                    YAML::Node vtx;
                    vtx["type"] = "VTX";
                    vtx["mio0"] = addr.value();
                    vtx["offset"] = ptr;
                    vtx["count"] = nvtx;
                    vtx["symbol"] = output;
                    auto result = factory->parse(rom, vtx);
                    if(result.has_value()){
                        Companion::Instance->RegisterAsset(output, vtx);
                    }
                }
                break;

        }

        if(opcode == static_cast<uint8_t>(G_ENDDL)) {
            processing = false;
        }
    }

    return std::make_shared<DListData>(gfxs);
}