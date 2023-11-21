#include "DisplayListFactory.h"
#include "DisplayListOverrides.h"
#include "utils/MIODecoder.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include <gfxd.h>
#include <fstream>
#include <utils/TorchUtils.h>

#define C0(pos, width) ((w0 >> (pos)) & ((1U << width) - 1))

void GFXDSetGBIVersion(){
    switch (Companion::Instance->GetGBIVersion()) {
        case GBIVersion::f3d:
            gfxd_target(gfxd_f3d);
            break;
        case GBIVersion::f3dex:
            gfxd_target(gfxd_f3dex);
            break;
        case GBIVersion::f3db:
            gfxd_target(gfxd_f3db);
            break;
        case GBIVersion::f3dex2:
            gfxd_target(gfxd_f3dex2);
            break;
        case GBIVersion::f3dexb:
            gfxd_target(gfxd_f3dexb);
            break;
    }
}

void DListHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = node["symbol"] ? node["symbol"].as<std::string>() : entryName;

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return;
    }

    write << "extern Gfx " << symbol << "[];\n";
}

void DListCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto cmds = std::static_pointer_cast<DListData>(raw)->mGfxs;
    const auto symbol = node["symbol"].as<std::string>();
    const auto offset = node["offset"].as<uint32_t>();
    char out[0xFFFF] = {0};

    gfxd_input_buffer(cmds.data(), sizeof(uint32_t) * cmds.size());
    gfxd_output_buffer(out, sizeof(out));

    gfxd_endian(gfxd_endian_host, sizeof(uint32_t));
    gfxd_macro_fn([] {
        auto gfx = static_cast<const Gfx*>(gfxd_macro_data());
        auto opcode = (gfx->words.w0 >> 24) & 0xFF;

        gfxd_puts(fourSpaceTab);

        #define QUAD 0xB5
        #define TRI2 0xB1

        switch(opcode) {

            // For mk64 only
            case QUAD:
                if (Companion::Instance->GetGBIMinorVersion() == GBIMinorVersion::Mk64) {
                    GFXDOverride::Quadrangle(gfx);
                } else {
                    gfxd_macro_dflt();
                }
                break;
            // Prevents mix and matching of quadrangle commands. Forces 2TRI only. 
            case TRI2:
                GFXDOverride::Triangle2(gfx);
                break;
            default:
                gfxd_macro_dflt();
                break;
        }

        gfxd_puts(",\n");
        return 0;
    });

    gfxd_vtx_callback(GFXDOverride::Vtx);
    gfxd_timg_callback(GFXDOverride::Texture);
    gfxd_dl_callback(GFXDOverride::DisplayList);
    gfxd_lightsn_callback(GFXDOverride::Light);
    GFXDSetGBIVersion();

    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::hex << std::uppercase << offset << "\n";
    }

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
    gfxd_vtx_callback(GFXDOverride::Vtx);
    gfxd_timg_callback(GFXDOverride::Texture);
    gfxd_dl_callback(GFXDOverride::DisplayList);
    //gfxd_light_callback(GFXDOverride::Light);
    GFXDSetGBIVersion();
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
    }

#undef case

    writer.Finish(write);
}

std::optional<std::shared_ptr<IParsedData>> DListFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto gbi = Companion::Instance->GetGBIVersion();

    const auto mio0 = node["mio0"].as<uint32_t>();
    const auto offset = node["offset"].as<int32_t>();
    auto decoded = MIO0Decoder::Decode(buffer, mio0);

    LUS::BinaryReader reader(decoded.data(), decoded.size());
    reader.SetEndianness(LUS::Endianness::Big);
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    std::vector<uint32_t> gfxs;
    auto processing = true;

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
                    SPDLOG_INFO("Addr to Display list command at 0x{:X} not in yaml, autogenerating it", w1);
                    auto addr = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(w1));
                    if(!addr.has_value()) {
                        SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", SEGMENT_NUMBER(w1));
                        break;
                    }

                    auto rom = Companion::Instance->GetRomData();
                    auto factory = Companion::Instance->GetFactory("GFX")->get();
                    std::string output = Companion::Instance->NormalizeAsset("seg" + std::to_string(SEGMENT_NUMBER(w1)) +"_dl_" + Torch::to_hex(w1, false));
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
            // Lights1
            case static_cast<uint8_t>(G_MOVEMEM): {
                uint32_t ptr = SEGMENT_OFFSET(w1);

                // Ignore if not a Lights1 command
                if ( ( (w0 >> 16) & 0xFF ) != G_MV_L1) {
                    break;
                }

                if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); !decl.has_value()){
                    SPDLOG_INFO("Addr to Lights1 command at 0x{:X} not in yaml, autogenerating it", w1);
                    auto addr = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(w1));
                    if(!addr.has_value()) {
                        SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", SEGMENT_NUMBER(w1));
                        break;
                    }
                    auto rom = Companion::Instance->GetRomData();
                    auto factory = Companion::Instance->GetFactory("LIGHTS")->get();
                    std::string output = Companion::Instance->NormalizeAsset("seg" + std::to_string(SEGMENT_NUMBER(w1)) +"_lights1_" + Torch::to_hex(w1, false));
                    YAML::Node light;
                    light["type"] = "lights";
                    light["mio0"] = addr.value();
                    light["offset"] = ptr;
                    light["symbol"] = output;
                    auto result = factory->parse(rom, light);
                    if(result.has_value()){
                        Companion::Instance->RegisterAsset(output, light);
                    }
                }
                break;
            }
            case static_cast<uint8_t>(G_VTX): {
                uint32_t nvtx;

                switch (gbi) {
                    case GBIVersion::f3dex2:
                        nvtx = C0(12, 8);
                        break;
                    case GBIVersion::f3dex:
                    case GBIVersion::f3dexb:
                        nvtx = C0(10, 6);
                        break;
                    default:
                        nvtx = (C0(0, 16)) / sizeof(Vtx);
                        break;
                }

                uint32_t ptr = SEGMENT_OFFSET(w1);

                if(const auto decl = Companion::Instance->GetNodeByAddr(ptr); !decl.has_value()){
                    SPDLOG_INFO("Addr to Vtx array at 0x{:X} not in yaml, autogenerating  it", w1);
                    auto addr = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(w1));
                    if(!addr.has_value()) {
                        SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", SEGMENT_NUMBER(w1));
                        break;
                    }

                    auto rom = Companion::Instance->GetRomData();
                    auto factory = Companion::Instance->GetFactory("VTX")->get();
                    std::string output = Companion::Instance->NormalizeAsset("seg" + std::to_string(SEGMENT_NUMBER(w1)) +"_vtx_" + Torch::to_hex(w1, false));
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
        }
    }

    return std::make_shared<DListData>(gfxs);
}