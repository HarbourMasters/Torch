#include "GeoLayoutFactory.h"

#include "spdlog/spdlog.h"
#include "geo/GeoCommand.h"
#include "Companion.h"
#include "geo/GeoUtils.h"
#include "utils/TorchUtils.h"

uint64_t RegisterOrLoadGeoLayout(uint32_t ptr) {

    auto dec = Companion::Instance->GetNodeByAddr(Torch::translate(ptr));
    if(dec.has_value()){
        uint64_t hash = CRC64(std::get<0>(dec.value()).c_str());
        SPDLOG_INFO("Found Geo Layout: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(dec.value()));
        return hash;
    }

    SPDLOG_INFO("Addr to Geo Layout at 0x{:X} not in yaml, autogenerating it", ptr);
    auto addr = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(ptr));
    if(!addr.has_value()) {
        SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", SEGMENT_NUMBER(ptr));
        return 0;
    }

    auto rom = Companion::Instance->GetRomData();
    auto factory = Companion::Instance->GetFactory("SM64:GEO_LAYOUT")->get();
    std::string output = Companion::Instance->NormalizeAsset("seg" + std::to_string(SEGMENT_NUMBER(ptr)) +"_geo_" + Torch::to_hex(ptr, false));
    YAML::Node dl;
    dl["type"] = "SM64:GEO_LAYOUT";
    dl["offset"] = ptr;
    dl["symbol"] = output;
    auto result = factory->parse(rom, dl);

    if(!result.has_value()){
        return 0;
    }

    auto ndec = Companion::Instance->RegisterAsset(output, dl);

    if(ndec.has_value()){
        uint64_t hash = CRC64(std::get<0>(ndec.value()).c_str());
        SPDLOG_INFO("Found Geo Layout: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(ndec.value()));
        return hash;
    }

    return 0;
}

uint64_t RegisterOrLoadDisplayList(uint32_t ptr) {

    auto dec = Companion::Instance->GetNodeByAddr(Torch::translate(ptr));
    if(dec.has_value()){
        uint64_t hash = CRC64(std::get<0>(dec.value()).c_str());
        SPDLOG_INFO("Found Display List: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(dec.value()));
        return hash;
    }

    SPDLOG_INFO("Addr to Display List at 0x{:X} not in yaml, autogenerating it", ptr);
    auto addr = Companion::Instance->GetSegmentedAddr(SEGMENT_NUMBER(ptr));
    if(!addr.has_value()) {
        SPDLOG_ERROR("Segment data missing from game config\nPlease add an entry for segment {}", SEGMENT_NUMBER(ptr));
        return 0;
    }

    auto rom = Companion::Instance->GetRomData();
    auto factory = Companion::Instance->GetFactory("GFX")->get();
    std::string output = Companion::Instance->NormalizeAsset("seg" + std::to_string(SEGMENT_NUMBER(ptr)) +"_dl_" + Torch::to_hex(ptr, false));
    YAML::Node dl;
    dl["type"] = "GFX";
    dl["mio0"] = addr.value();
    dl["offset"] = SEGMENT_OFFSET(ptr);
    dl["symbol"] = output;
    auto result = factory->parse(rom, dl);

    if(!result.has_value()){
        return 0;
    }

    auto ndec = Companion::Instance->RegisterAsset(output, dl);

    if(ndec.has_value()){
        uint64_t hash = CRC64(std::get<0>(ndec.value()).c_str());
        SPDLOG_INFO("Found Display List: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(ndec.value()));
        return hash;
    }

    return 0;
}

void SM64::GeoBinaryExporter::Export(std::ostream&write, std::shared_ptr<IParsedData> data, std::string&entryName, YAML::Node&node, std::string* replacement) {
    const auto layout = std::static_pointer_cast<GeoLayout>(data).get();

    auto writer = LUS::BinaryWriter();

    for(auto& [opcode, arguments] : layout->commands) {
        writer.Write(static_cast<uint8_t>(opcode));

        for(auto& args : arguments) {
            switch(args.index()) {
                case 0: {
                    writer.Write(std::get<uint8_t>(args));
                    break;
                }
                case 1: {
                    writer.Write(std::get<int8_t>(args));
                    break;
                }
                case 2: {
                    writer.Write(std::get<uint16_t>(args));
                    break;
                }
                case 3: {
                    writer.Write(std::get<int16_t>(args));
                    break;
                }
                case 4: {
                    writer.Write(std::get<uint32_t>(args));
                    break;
                }
                case 5: {
                    writer.Write(std::get<int32_t>(args));
                    break;
                }
                case 6: {
                    writer.Write(std::get<uint64_t>(args));
                    break;
                }
                case 7: {
                    const auto [x, y] = std::get<Vec2f>(args);
                    writer.Write(x);
                    writer.Write(y);
                    break;
                }
                case 8: {
                    const auto [x, y, z] = std::get<Vec3f>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    break;
                }
                case 9: {
                    const auto [x, y, z] = std::get<Vec3s>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    break;
                }
                case 10: {
                    const auto [x, y, z] = std::get<Vec3i>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    break;
                }
                case 11: {
                    const auto [x, y, z, w] = std::get<Vec4f>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    writer.Write(w);
                    break;
                }
                case 12: {
                    const auto [x, y, z, w] = std::get<Vec4s>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    writer.Write(w);
                    break;
                }
                default: {
                    break;
                }
            }
        }
    }

    std::vector<char> buffer = writer.ToVector();
    writer.Close();

    LUS::BinaryWriter output = LUS::BinaryWriter();
    WriteHeader(output, LUS::ResourceType::Blob, 0);

    output.Write(static_cast<uint32_t>(buffer.size()));
    output.Write(buffer.data(), buffer.size());
    output.Finish(write);
    output.Close();
}

void SM64::GeoHeaderExporter::Export(std::ostream&write, std::shared_ptr<IParsedData> data, std::string&entryName, YAML::Node&node, std::string* replacement) {

}

std::optional<std::shared_ptr<IParsedData>> SM64::GeoLayoutFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const size_t offset = Torch::translate(GetSafeNode<uint32_t>(node, "offset"));
    auto cmd = buffer.data() + offset;
    bool processing = true;
    std::vector<GeoCommand> commands;

    while(processing) {
        auto opcode = static_cast<GeoOpcode>(cmd[0x00]);

        SPDLOG_INFO("Processing Command {}", opcode);

        std::vector<GeoArgument> arguments;

        switch(opcode){
            case GeoOpcode::BranchAndLink: {
                uint32_t ptr = cur_geo_cmd_u32(0x04);
                arguments.emplace_back(ptr);
                arguments.emplace_back(RegisterOrLoadGeoLayout(ptr));

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::End: {
                processing = false;
                break;
            }
            case GeoOpcode::Branch: {
                auto jmp = cur_geo_cmd_u8(0x01);
                auto ptr = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(jmp);
                arguments.emplace_back(RegisterOrLoadGeoLayout(ptr));

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::Return:
            case GeoOpcode::OpenNode:
            case GeoOpcode::CloseNode: {
                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::AssignAsView: {
                auto idx = cur_geo_cmd_s16(0x02);
                arguments.emplace_back(idx);

                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::UpdateNodeFlags: {
                auto operation = cur_geo_cmd_u8(0x01);
                auto flags = cur_geo_cmd_s16(0x02);
                arguments.emplace_back(operation);
                arguments.emplace_back(flags);

                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeRoot: {
                auto views = cur_geo_cmd_s16(0x02);
                auto x = cur_geo_cmd_s16(0x04);
                auto y = cur_geo_cmd_s16(0x06);
                auto width = cur_geo_cmd_s16(0x08);
                auto height = cur_geo_cmd_s16(0x0A);

                arguments.emplace_back(views);
                arguments.emplace_back(x);
                arguments.emplace_back(y);
                arguments.emplace_back(width);
                arguments.emplace_back(height);

                cmd += 0x0C << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeOrthoProjection: {
                auto scale = cur_geo_cmd_s16(0x02);
                arguments.emplace_back(scale);

                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodePerspective: {
                auto opt = cur_geo_cmd_u8(0x01);
                auto fov = cur_geo_cmd_s16(0x02);
                auto near = cur_geo_cmd_s16(0x04);
                auto far = cur_geo_cmd_s16(0x06);

                arguments.emplace_back(opt);
                arguments.emplace_back(fov);
                arguments.emplace_back(near);
                arguments.emplace_back(far);

                if (opt != 0) {
                    // optional asm function
                    auto ptr = cur_geo_cmd_u32(0x08);
                    arguments.emplace_back(ptr);

                    cmd += 0x04 << CMD_SIZE_SHIFT;
                }

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeStart: {
                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeMasterList: {
                auto list = cur_geo_cmd_u8(0x01);
                arguments.emplace_back(list);

                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeLevelOfDetail: {
                auto min = cur_geo_cmd_s16(0x02);
                auto max = cur_geo_cmd_s16(0x04);

                arguments.emplace_back(min);
                arguments.emplace_back(max);

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeSwitchCase: {
                auto cs = cur_geo_cmd_s16(0x02);
                auto ptr = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(cs);
                arguments.emplace_back(ptr);

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeCamera: {
                auto cmd_pos = reinterpret_cast<int16_t*>(&cmd[4]);

                Vec3f pos = {};
                Vec3f focus = {};
                cmd_pos = read_vec3s_to_vec3f(pos, cmd_pos);
                read_vec3s_to_vec3f(focus, cmd_pos);

                auto ptr = cur_geo_cmd_u32(0x10);
                auto type = cur_geo_cmd_s16(0x02);

                arguments.emplace_back(pos);
                arguments.emplace_back(focus);
                arguments.emplace_back(ptr);
                arguments.emplace_back(type);

                cmd += 0x14 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeTranslationRotation: {

                Vec3s translation = {};
                Vec3s rotation = {};
                auto params = cur_geo_cmd_u8(0x01);
                auto cmd_pos = reinterpret_cast<int16_t *>(&cmd[0x04]);

                arguments.emplace_back(params);

                switch ((params & 0x70) >> 4) {
                    case 0:
                        cmd_pos = read_vec3s(translation, &cmd_pos[2]);
                        cmd_pos = read_vec3s_angle(rotation, cmd_pos);
                        arguments.emplace_back(translation);
                        arguments.emplace_back(rotation);
                        break;
                    case 1:
                        cmd_pos = read_vec3s(translation, &cmd_pos[1]);
                        arguments.emplace_back(translation);
                        break;
                    case 2:
                        cmd_pos = read_vec3s_angle(rotation, &cmd_pos[1]);
                        arguments.emplace_back(rotation);
                        break;
                    case 3:
                        arguments.emplace_back(cmd_pos[1]);
                        cmd_pos += 2 << CMD_SIZE_SHIFT;
                        break;
                    default: {
                        break;
                    }
                }

                if (params & 0x80) {
                    auto ptr = BSWAP32(*reinterpret_cast<uint32_t*>(&cmd_pos[0]));
                    arguments.emplace_back(RegisterOrLoadDisplayList(ptr));
                    cmd_pos += 2 << CMD_SIZE_SHIFT;
                }

                cmd = reinterpret_cast<uint8_t *>(cmd_pos);
                break;
            }
            case GeoOpcode::NodeTranslation:
            case GeoOpcode::NodeRotation: {
                Vec3s vector = {};
                auto params = cur_geo_cmd_u8(0x01);
                auto cmd_pos = reinterpret_cast<int16_t *>(cmd);

                arguments.emplace_back(params);

                cmd_pos = read_vec3s_angle(vector, &cmd_pos[1]);

                arguments.emplace_back(vector);

                if (params & 0x80) {
                    auto ptr = BSWAP32(*reinterpret_cast<uint32_t*>(&cmd_pos[0]));
                    arguments.emplace_back(RegisterOrLoadDisplayList(ptr));
                    cmd_pos += 2 << CMD_SIZE_SHIFT;
                }

                cmd = reinterpret_cast<uint8_t *>(cmd_pos);
                break;
            }
            case GeoOpcode::NodeAnimatedPart: {

                Vec3s translation = {};
                auto layer = cur_geo_cmd_u8(0x01);
                auto  ptr = cur_geo_cmd_u32(0x08);
                auto cmd_pos = reinterpret_cast<int16_t*>(cmd);

                arguments.emplace_back(layer);
                arguments.emplace_back(RegisterOrLoadDisplayList(ptr));

                read_vec3s(translation, &cmd_pos[1]);
                arguments.emplace_back(translation);

                cmd += 0x0C << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeBillboard: {

                Vec3s translation = {};
                auto params = cur_geo_cmd_u8(0x01);
                auto cmd_pos = reinterpret_cast<int16_t*>(cmd);

                read_vec3s(translation, &cmd_pos[1]);

                arguments.emplace_back(params);
                arguments.emplace_back(translation);

                if (params & 0x80) {
                    auto ptr = BSWAP32(*reinterpret_cast<uint32_t*>(&cmd_pos[0]));
                    arguments.emplace_back(RegisterOrLoadDisplayList(ptr));
                    cmd_pos += 0x02 << CMD_SIZE_SHIFT;
                }

                cmd = reinterpret_cast<uint8_t*>(cmd_pos);
                break;
            }
            case GeoOpcode::NodeDisplayList: {
                auto layer = cur_geo_cmd_u8(0x01);
                auto ptr = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(layer);
                arguments.emplace_back(RegisterOrLoadDisplayList(ptr));

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeShadow: {
                auto type = cur_geo_cmd_s16(0x02);
                auto solidity = cur_geo_cmd_s16(0x04);
                auto scale = cur_geo_cmd_s16(0x06);

                arguments.emplace_back(type);
                arguments.emplace_back(solidity);
                arguments.emplace_back(scale);

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeObjectParent: {
                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeAsm: {
                auto param = cur_geo_cmd_s16(0x02);
                auto ptr = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(param);
                arguments.emplace_back(ptr);
                SPDLOG_INFO("Found ASM: 0x{:X}", ptr);

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeBackground: {
                auto bg = cur_geo_cmd_s16(0x02);
                auto ptr = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(bg);
                arguments.emplace_back(ptr);

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NOP: {
                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::CopyView: {
                auto idx = cur_geo_cmd_s16(0x02);

                arguments.emplace_back(idx);

                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeHeldObj: {
                auto ptr = cur_geo_cmd_u32(0x08);
                auto player = cur_geo_cmd_u8(0x01);

                arguments.emplace_back(ptr);
                arguments.emplace_back(player);

                Vec3s vec = {};
                read_vec3s(vec, reinterpret_cast<int16_t*>(&cmd[0x02]));

                arguments.emplace_back(vec);

                cmd += 0x0C << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeScale: {
                auto params = cur_geo_cmd_u8(0x01);
                auto scale  = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(params);
                arguments.emplace_back(scale);

                if (params & 0x80) {
                    auto ptr = cur_geo_cmd_u32(0x08);
                    arguments.emplace_back(RegisterOrLoadDisplayList(ptr));
                    cmd += 0x04 << CMD_SIZE_SHIFT;
                }

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NOP2: {
                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NOP3: {
                cmd += 0x10 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeCullingRadius: {
                auto radius = cur_geo_cmd_s16(0x02);

                arguments.emplace_back(radius);

                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            default: {
                SPDLOG_ERROR("Unknown geo command {}", opcode);
                throw std::runtime_error("Unknown geo command");
            }
        }

        commands.push_back({opcode, arguments});
    }

    return std::make_shared<GeoLayout>(commands);
}
