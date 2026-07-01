#include "GeoLayoutFactory.h"

#include "spdlog/spdlog.h"
#include "geo/GeoCommand.h"
#include "Companion.h"
#include "geo/GeoUtils.h"
#include "utils/TorchUtils.h"

#include <regex>

std::unordered_map<uint32_t, std::string> gFunctionMap;
static std::regex pattern(R"(0x([0-9a-fA-F]+)\s{16}([^\s]+))");

uint64_t RegisterAutoGen(uint32_t ptr, std::string type) {

    if (ptr != 0) {
        YAML::Node node;
        node["type"] = type;
        node["offset"] = ptr;
        Companion::Instance->AddAsset(node);
    } else {
        SPDLOG_WARN("RegisterAutoGen: ptr is 0 type: {}", type);
    }

    return ptr;
}

void StoreFunc(uint32_t vram) {
    return;

    if (!Torch::contains(gFunctionMap, vram)) {
        return;
    }

    auto name = gFunctionMap[vram];
    SPDLOG_INFO("Found Function: 0x{:X} Name: {}", vram, name);

    std::ofstream outfile;
    outfile.open("map.txt", std::ios_base::app);
    outfile << "{ 0x" << std::hex << vram << ", " << name << " },\n";

    gFunctionMap.erase(vram);
}

SM64::GeoLayoutFactory::GeoLayoutFactory() {
    // std::ifstream file("/Users/lywx/Downloads/sm64.jp.map");
    // std::string str;
    // while (std::getline(file, str)) {
    //     if(str.find('=') != std::string::npos) {
    //         continue;
    //     }

    //     std::smatch match;
    //     if (std::regex_search(str, match, pattern)) {
    //         gFunctionMap[std::stoul(match[1].str(), nullptr, 16)] = match[2].str();
    //     }
    // }
}

ExportResult SM64::GeoCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> data,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto cmds = std::static_pointer_cast<GeoLayout>(data)->commands;
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    uint32_t indentCount = 1;
    uint32_t cmdCount = 0;

    write << "GeoLayout " << symbol << "[] = {\n";

    for (auto& [opcode, arguments, skip] : cmds) {
        bool commaFlag = false;

        if (opcode == GeoOpcode::OpenNode) {
            ++indentCount;
        }

        for (uint32_t i = 0; i < indentCount; ++i) {
            write << fourSpaceTab;
        }

        if (opcode == GeoOpcode::CloseNode) {
            --indentCount;
        }

        write << opcode << "(";
        for (auto& args : arguments) {
            if (commaFlag) {
                write << ", ";
            } else {
                commaFlag = true;
            }

            switch (static_cast<GeoArgumentType>(args.index())) {
                case GeoArgumentType::U8: {
                    write << std::hex << "0x" << static_cast<uint32_t>(std::get<uint8_t>(args));
                    break;
                }
                case GeoArgumentType::S8: {
                    write << std::hex << "0x" << static_cast<uint32_t>(std::get<int8_t>(args));
                    break;
                }
                case GeoArgumentType::U16: {
                    write << std::hex << "0x" << std::get<uint16_t>(args);
                    break;
                }
                case GeoArgumentType::S16: {
                    write << std::dec << std::get<int16_t>(args);
                    break;
                }
                case GeoArgumentType::U32: {
                    write << std::hex << "0x" << std::get<uint32_t>(args);
                    break;
                }
                case GeoArgumentType::S32: {
                    write << std::dec << std::get<int32_t>(args);
                    break;
                }
                case GeoArgumentType::U64: {
                    // write << std::hex << "0x" << std::get<uint64_t>(args);
                    uint32_t ptr = std::get<uint64_t>(args);
                    auto dec = Companion::Instance->GetNodeByAddr(ptr);
                    std::string symbol = "NULL";

                    if (dec.has_value()) {
                        auto node = std::get<1>(dec.value());
                        symbol = GetSafeNode<std::string>(node, "symbol");
                        write << symbol;
                    } else if (ptr == 0) {
                        write << symbol;
                    } else {
                        SPDLOG_WARN("Cannot find node for ptr 0x{:X}", ptr);
                        write << std::hex << "0x" << ptr;
                    }
                    break;
                }
                case GeoArgumentType::VEC2F: {
                    const auto [x, y] = std::get<Vec2f>(args);
                    write << std::dec << x << ", " << y;
                    break;
                }
                case GeoArgumentType::VEC3F: {
                    const auto [x, y, z] = std::get<Vec3f>(args);
                    write << std::dec << x << ", " << y << ", " << z;
                    break;
                }
                case GeoArgumentType::VEC3S: {
                    const auto [x, y, z] = std::get<Vec3s>(args);
                    write << std::dec << x << ", " << y << ", " << z;
                    break;
                }
                case GeoArgumentType::VEC3I: {
                    const auto [x, y, z] = std::get<Vec3i>(args);
                    write << std::dec << x << ", " << y << ", " << z;
                    break;
                }
                case GeoArgumentType::VEC4F: {
                    const auto [x, y, z, w] = std::get<Vec4f>(args);
                    write << std::dec << x << ", " << y << ", " << z << ", " << w;
                    break;
                }
                case GeoArgumentType::VEC4S: {
                    const auto [x, y, z, w] = std::get<Vec4s>(args);
                    write << std::dec << x << ", " << y << ", " << z << ", " << w;
                    break;
                }
                case GeoArgumentType::STRING: {
                    write << std::get<std::string>(args);
                    break;
                }
                default: {
                    break;
                }
            }
        }
        if (skip) {
            write << "), //! more close than open nodes\n";
        } else {
            write << "),\n";
        }
        ++cmdCount;
    }

    write << "};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// count: " << std::to_string(cmdCount) << " GeoLayout\n";
    } else {
        write << "\n";
    }

    return std::nullopt;
}

ExportResult SM64::GeoBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> data,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto layout = std::static_pointer_cast<GeoLayout>(data).get();

    auto writer = LUS::BinaryWriter();

    for (auto& [opcode, arguments, skip] : layout->commands) {
        if (skip) {
            opcode = GeoOpcode::End;
            arguments.clear();
        }
        writer.Write(static_cast<uint8_t>(opcode));

        for (auto& args : arguments) {
            switch (static_cast<GeoArgumentType>(args.index())) {
                case GeoArgumentType::U8: {
                    writer.Write(std::get<uint8_t>(args));
                    break;
                }
                case GeoArgumentType::S8: {
                    writer.Write(std::get<int8_t>(args));
                    break;
                }
                case GeoArgumentType::U16: {
                    writer.Write(std::get<uint16_t>(args));
                    break;
                }
                case GeoArgumentType::S16: {
                    writer.Write(std::get<int16_t>(args));
                    break;
                }
                case GeoArgumentType::U32: {
                    writer.Write(std::get<uint32_t>(args));
                    break;
                }
                case GeoArgumentType::S32: {
                    writer.Write(std::get<int32_t>(args));
                    break;
                }
                case GeoArgumentType::U64: {
                    auto ptr = std::get<uint64_t>(args);
                    auto dec = Companion::Instance->GetNodeByAddr(ptr);
                    if (ptr == 0) {
                        writer.Write((uint64_t)0);
                    } else if (dec.has_value()) {
                        uint64_t hash = CRC64(std::get<0>(dec.value()).c_str());
                        SPDLOG_INFO("Found Asset: 0x{:X} Hash: 0x{:X} Path: {}", ptr, hash, std::get<0>(dec.value()));
                        writer.Write(hash);
                    } else {
                        SPDLOG_WARN("Could not find Asset at 0x{:X}", ptr);
                    }
                    break;
                }
                case GeoArgumentType::VEC2F: {
                    const auto [x, y] = std::get<Vec2f>(args);
                    writer.Write(x);
                    writer.Write(y);
                    break;
                }
                case GeoArgumentType::VEC3F: {
                    const auto [x, y, z] = std::get<Vec3f>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    break;
                }
                case GeoArgumentType::VEC3S: {
                    const auto [x, y, z] = std::get<Vec3s>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    break;
                }
                case GeoArgumentType::VEC3I: {
                    const auto [x, y, z] = std::get<Vec3i>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    break;
                }
                case GeoArgumentType::VEC4F: {
                    const auto [x, y, z, w] = std::get<Vec4f>(args);
                    writer.Write(x);
                    writer.Write(y);
                    writer.Write(z);
                    writer.Write(w);
                    break;
                }
                case GeoArgumentType::VEC4S: {
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
    WriteHeader(output, Torch::ResourceType::Blob, 0);

    output.Write(static_cast<uint32_t>(buffer.size()));
    output.Write(buffer.data(), buffer.size());
    output.Finish(write);
    output.Close();
    return std::nullopt;
}

ExportResult SM64::GeoHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> data,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern GeoLayout " << symbol << "[];\n";

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SM64::GeoLayoutFactory::parse(std::vector<uint8_t>& buffer,
                                                                          YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    auto cmd = segment.data;

    bool processing = true;
    int32_t openCount = 0;
    std::vector<GeoCommand> commands;

    while (processing) {
        auto opcode = static_cast<GeoOpcode>(cmd[0x00]);
        auto skip = false;

        SPDLOG_INFO("Processing Command {}", opcode);

        std::vector<GeoArgument> arguments;

        switch (opcode) {
            case GeoOpcode::BranchAndLink: {
                auto ptr = cur_geo_cmd_u32(0x04);
                if (ptr == 0) {
                    processing = false;
                }
                arguments.emplace_back(RegisterAutoGen(ptr, "SM64:GEO_LAYOUT"));

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
                arguments.emplace_back(RegisterAutoGen(ptr, "SM64:GEO_LAYOUT"));

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::Return: {
                processing = false;
                break;
            }
            case GeoOpcode::OpenNode: {
                openCount++;
                cmd += 0x04 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::CloseNode: {
                cmd += 0x04 << CMD_SIZE_SHIFT;
                if (openCount - 1 < 0) {
                    skip = true;
                } else {
                    openCount--;
                }
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

                    StoreFunc(ptr);

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
                auto min = cur_geo_cmd_s16(0x04);
                auto max = cur_geo_cmd_s16(0x06);

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

                StoreFunc(ptr);

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

                arguments.emplace_back(type);
                arguments.emplace_back(pos);
                arguments.emplace_back(focus);
                arguments.emplace_back(ptr);

                StoreFunc(ptr);

                cmd += 0x14 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeTranslationRotation: {

                Vec3s translation = {};
                Vec3s rotation = {};
                auto params = cur_geo_cmd_u8(0x01);
                auto cmd_pos = reinterpret_cast<int16_t*>(cmd);

                arguments.emplace_back(params);

                switch ((params & 0x70) >> 4) {
                    case 0:
                        cmd_pos = read_vec3s(translation, &cmd_pos[2]);
                        cmd_pos = read_vec3s(rotation, cmd_pos);
                        arguments.emplace_back(translation);
                        arguments.emplace_back(rotation);
                        break;
                    case 1:
                        cmd_pos = read_vec3s(translation, &cmd_pos[1]);
                        arguments.emplace_back(translation);
                        break;
                    case 2:
                        cmd_pos = read_vec3s(rotation, &cmd_pos[1]);
                        arguments.emplace_back(rotation);
                        break;
                    case 3:
                        arguments.emplace_back(cmd_pos[1]);
                        cmd_pos += 0x02 << CMD_SIZE_SHIFT;
                        break;
                    default: {
                        break;
                    }
                }

                if (params & 0x80) {
                    auto ptr = BSWAP32(*reinterpret_cast<uint32_t*>(&cmd_pos[0]));
                    arguments.emplace_back(RegisterAutoGen(ptr, "GFX"));
                    cmd_pos += 2 << CMD_SIZE_SHIFT;
                }

                cmd = reinterpret_cast<uint8_t*>(cmd_pos);
                break;
            }
            case GeoOpcode::NodeTranslation:
            case GeoOpcode::NodeRotation: {
                Vec3s vector = {};
                auto params = cur_geo_cmd_u8(0x01);
                auto cmd_pos = reinterpret_cast<int16_t*>(cmd);

                arguments.emplace_back(params);

                cmd_pos = read_vec3s(vector, &cmd_pos[1]);

                arguments.emplace_back(vector);

                if (params & 0x80) {
                    auto ptr = BSWAP32(*reinterpret_cast<uint32_t*>(&cmd_pos[0]));
                    arguments.emplace_back(RegisterAutoGen(ptr, "GFX"));
                    cmd_pos += 2 << CMD_SIZE_SHIFT;
                }

                cmd = reinterpret_cast<uint8_t*>(cmd_pos);
                break;
            }
            case GeoOpcode::NodeAnimatedPart: {
                Vec3s translation = {};
                auto layer = cur_geo_cmd_u8(0x01);
                auto ptr = cur_geo_cmd_u32(0x08);
                auto cmd_pos = reinterpret_cast<int16_t*>(cmd);

                arguments.emplace_back(layer);

                read_vec3s(translation, &cmd_pos[1]);
                arguments.emplace_back(translation);

                arguments.emplace_back(RegisterAutoGen(ptr, "GFX"));

                cmd += 0x0C << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeBillboard: {

                Vec3s translation = {};
                auto params = cur_geo_cmd_u8(0x01);
                auto cmd_pos = reinterpret_cast<int16_t*>(cmd);

                cmd_pos = read_vec3s(translation, &cmd_pos[1]);

                arguments.emplace_back(params);
                arguments.emplace_back(translation);

                if (params & 0x80) {
                    auto ptr = BSWAP32(*reinterpret_cast<uint32_t*>(&cmd_pos[0]));
                    arguments.emplace_back(RegisterAutoGen(ptr, "GFX"));
                    cmd_pos += 0x02 << CMD_SIZE_SHIFT;
                }

                cmd = reinterpret_cast<uint8_t*>(cmd_pos);
                break;
            }
            case GeoOpcode::NodeDisplayList: {
                auto layer = cur_geo_cmd_u8(0x01);
                auto ptr = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(layer);
                arguments.emplace_back(RegisterAutoGen(ptr, "GFX"));

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
                StoreFunc(ptr);

                cmd += 0x08 << CMD_SIZE_SHIFT;
                break;
            }
            case GeoOpcode::NodeBackground: {
                auto bg = cur_geo_cmd_s16(0x02);
                auto ptr = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(bg);
                arguments.emplace_back(ptr);

                StoreFunc(ptr);

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

                StoreFunc(ptr);

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
                auto scale = cur_geo_cmd_u32(0x04);

                arguments.emplace_back(params);
                arguments.emplace_back(scale);

                if (params & 0x80) {
                    auto ptr = cur_geo_cmd_u32(0x08);
                    arguments.emplace_back(RegisterAutoGen(ptr, "GFX"));
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
                SPDLOG_ERROR("Unknown geo command '{}'", opcode);
                throw std::runtime_error("Unknown geo command " + std::to_string(static_cast<uint8_t>(opcode)));
            }
        }

        commands.push_back({ opcode, arguments, skip });
    }

    return std::make_shared<GeoLayout>(commands);
}

#ifdef BUILD_UI
#include <algorithm>
#include <cmath>
#include <cstring>

#include "ui/BaseBackend.h"
#include "ui/Widgets.h"

namespace {

// 4x4 in row-vector convention (v' = v * M), matching the N64 Mat4 layout:
// rotation in the upper 3x3, translation in row 3.
struct Mat4 {
    float m[4][4];
};

Mat4 Mat4Identity() {
    Mat4 r{};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}

// r = a * b (a applied first).
Mat4 Mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

// SM64's mtxf_rotate_zxy_and_translate; rotation given in degrees (the geo
// format stores degrees, converted to binary angles at runtime).
Mat4 Mat4RotZXYTranslate(const Vec3s& t, const Vec3s& rotDeg) {
    const float d2r = (float)M_PI / 180.0f;
    const float sx = std::sin(rotDeg.x * d2r), cx = std::cos(rotDeg.x * d2r);
    const float sy = std::sin(rotDeg.y * d2r), cy = std::cos(rotDeg.y * d2r);
    const float sz = std::sin(rotDeg.z * d2r), cz = std::cos(rotDeg.z * d2r);

    Mat4 r = Mat4Identity();
    r.m[0][0] = cy * cz + sx * sy * sz;
    r.m[1][0] = -cy * sz + sx * sy * cz;
    r.m[2][0] = cx * sy;
    r.m[3][0] = t.x;
    r.m[0][1] = cx * sz;
    r.m[1][1] = cx * cz;
    r.m[2][1] = -sx;
    r.m[3][1] = t.y;
    r.m[0][2] = -sy * cz + sx * cy * sz;
    r.m[1][2] = sy * sz + sx * cy * cz;
    r.m[2][2] = cx * cy;
    r.m[3][2] = t.z;
    return r;
}

Mat4 Mat4Scale(float s) {
    Mat4 r = Mat4Identity();
    r.m[0][0] = r.m[1][1] = r.m[2][2] = s;
    return r;
}

const uint8_t* ArgU8(const std::vector<GeoArgument>& args, size_t i) {
    return i < args.size() ? std::get_if<uint8_t>(&args[i]) : nullptr;
}
const int16_t* ArgS16(const std::vector<GeoArgument>& args, size_t i) {
    return i < args.size() ? std::get_if<int16_t>(&args[i]) : nullptr;
}
const uint32_t* ArgU32(const std::vector<GeoArgument>& args, size_t i) {
    return i < args.size() ? std::get_if<uint32_t>(&args[i]) : nullptr;
}
const uint64_t* ArgU64(const std::vector<GeoArgument>& args, size_t i) {
    return i < args.size() ? std::get_if<uint64_t>(&args[i]) : nullptr;
}
const Vec3s* ArgVec3s(const std::vector<GeoArgument>& args, size_t i) {
    return i < args.size() ? std::get_if<Vec3s>(&args[i]) : nullptr;
}

bool IsNodeCommand(GeoOpcode op) {
    switch (op) {
        case GeoOpcode::NodeRoot:
        case GeoOpcode::NodeOrthoProjection:
        case GeoOpcode::NodePerspective:
        case GeoOpcode::NodeStart:
        case GeoOpcode::NodeMasterList:
        case GeoOpcode::NodeLevelOfDetail:
        case GeoOpcode::NodeSwitchCase:
        case GeoOpcode::NodeCamera:
        case GeoOpcode::NodeTranslationRotation:
        case GeoOpcode::NodeTranslation:
        case GeoOpcode::NodeRotation:
        case GeoOpcode::NodeAnimatedPart:
        case GeoOpcode::NodeBillboard:
        case GeoOpcode::NodeDisplayList:
        case GeoOpcode::NodeShadow:
        case GeoOpcode::NodeObjectParent:
        case GeoOpcode::NodeAsm:
        case GeoOpcode::NodeBackground:
        case GeoOpcode::CopyView:
        case GeoOpcode::NodeHeldObj:
        case GeoOpcode::NodeScale:
        case GeoOpcode::NodeCullingRadius:
            return true;
        default:
            return false;
    }
}

// Mirrors rendering_graph_node.c enough for a bind-pose preview: a transform
// stack accumulated per node (child world = local * parent), only the first
// case of a switch, animated parts at their rest translation, billboards as
// plain translations. Camera/shadow/ASM/etc. nodes pass through untransformed.
struct GeoWalk {
    std::vector<UI::ModelPart> parts;
    std::vector<Mat4> stack{ Mat4Identity() };
    struct Frame {
        bool switchMode = false;
        int children = 0;
    };
    std::vector<Frame> frames{ Frame{} };
    Mat4 lastLocal = Mat4Identity();
    bool lastWasSwitch = false;
    bool pendingSkip = false; // last node culled; skip its subtree if one opens
    int skipDepth = -1;       // raw depth to return to before resuming
    int rawDepth = 0;         // open/close nesting, counted even while skipping
    const std::string* file = nullptr;
    int branchDepth = 0;
};

const ParseResultData* FindResultByName(const std::string& name) {
    for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
        for (const auto& r : results) {
            if (r.name == name) {
                return &r;
            }
        }
    }
    return nullptr;
}

std::optional<std::string> ResolveAddr(uint64_t ptr, const std::string& file) {
    if (ptr == 0) {
        return std::nullopt;
    }
    auto node = Companion::Instance->GetNodeByAddr((uint32_t)ptr, file);
    if (!node.has_value()) {
        return std::nullopt;
    }
    return std::get<0>(node.value());
}

void WalkGeoCommands(const std::vector<SM64::GeoCommand>& cmds, GeoWalk& ctx);

void WalkGeoTarget(uint64_t ptr, GeoWalk& ctx) {
    if (ctx.branchDepth > 8) {
        return;
    }
    const auto name = ResolveAddr(ptr, *ctx.file);
    if (!name.has_value()) {
        return;
    }
    const ParseResultData* target = FindResultByName(name.value());
    if (target == nullptr || !target->data.has_value() || target->type != "SM64:GEO_LAYOUT") {
        return;
    }
    auto geo = std::static_pointer_cast<SM64::GeoLayout>(target->data.value());
    if (geo == nullptr) {
        return;
    }
    ctx.branchDepth++;
    WalkGeoCommands(geo->commands, ctx);
    ctx.branchDepth--;
}

void WalkGeoCommands(const std::vector<SM64::GeoCommand>& cmds, GeoWalk& ctx) {
    for (const auto& cmd : cmds) {
        // While culling a switch's non-selected child, only track nesting.
        if (ctx.skipDepth >= 0) {
            if (cmd.opcode == GeoOpcode::OpenNode) {
                ctx.rawDepth++;
            } else if (cmd.opcode == GeoOpcode::CloseNode) {
                ctx.rawDepth--;
                if (ctx.rawDepth <= ctx.skipDepth) {
                    ctx.skipDepth = -1;
                }
            } else if (cmd.opcode == GeoOpcode::End || cmd.opcode == GeoOpcode::Return || cmd.skipped) {
                return;
            }
            continue;
        }
        if (cmd.skipped) {
            return; // unbalanced stream; exported as GEO_END
        }

        switch (cmd.opcode) {
            case GeoOpcode::OpenNode: {
                ctx.rawDepth++;
                if (ctx.pendingSkip) {
                    ctx.pendingSkip = false;
                    ctx.skipDepth = ctx.rawDepth - 1;
                    break;
                }
                ctx.stack.push_back(Mat4Mul(ctx.lastLocal, ctx.stack.back()));
                ctx.frames.push_back({ ctx.lastWasSwitch, 0 });
                ctx.lastLocal = Mat4Identity();
                ctx.lastWasSwitch = false;
                break;
            }
            case GeoOpcode::CloseNode: {
                ctx.rawDepth--;
                if (ctx.stack.size() > 1) {
                    ctx.stack.pop_back();
                    ctx.frames.pop_back();
                }
                ctx.lastLocal = Mat4Identity();
                ctx.lastWasSwitch = false;
                ctx.pendingSkip = false;
                break;
            }
            case GeoOpcode::End:
            case GeoOpcode::Return: {
                return;
            }
            case GeoOpcode::Branch: {
                const auto jmp = ArgU8(cmd.arguments, 0);
                const auto ptr = ArgU64(cmd.arguments, 1);
                if (ptr != nullptr) {
                    WalkGeoTarget(*ptr, ctx);
                }
                if (jmp == nullptr || *jmp != 1) {
                    return; // branch without return replaces this stream
                }
                break;
            }
            case GeoOpcode::BranchAndLink: {
                const auto ptr = ArgU64(cmd.arguments, 0);
                if (ptr != nullptr) {
                    WalkGeoTarget(*ptr, ctx);
                }
                break;
            }
            default: {
                if (!IsNodeCommand(cmd.opcode)) {
                    break;
                }
                ctx.pendingSkip = false;
                GeoWalk::Frame& parent = ctx.frames.back();
                parent.children++;
                if (parent.switchMode && parent.children > 1) {
                    // Only the first switch case previews (no game state to pick).
                    ctx.pendingSkip = true;
                    ctx.lastLocal = Mat4Identity();
                    ctx.lastWasSwitch = false;
                    break;
                }

                Mat4 local = Mat4Identity();
                uint64_t dlPtr = 0;
                uint8_t dlLayer = 1; // LAYER_OPAQUE
                const auto& args = cmd.arguments;
                switch (cmd.opcode) {
                    case GeoOpcode::NodeTranslationRotation: {
                        const auto params = ArgU8(args, 0);
                        if (params == nullptr) {
                            break;
                        }
                        Vec3s trans{}, rot{};
                        size_t next = 1;
                        switch ((*params & 0x70) >> 4) {
                            case 0:
                                if (const auto t = ArgVec3s(args, 1)) trans = *t;
                                if (const auto r = ArgVec3s(args, 2)) rot = *r;
                                next = 3;
                                break;
                            case 1:
                                if (const auto t = ArgVec3s(args, 1)) trans = *t;
                                next = 2;
                                break;
                            case 2:
                                if (const auto r = ArgVec3s(args, 1)) rot = *r;
                                next = 2;
                                break;
                            case 3:
                                if (const auto y = ArgS16(args, 1)) rot = Vec3s(0, *y, 0);
                                next = 2;
                                break;
                        }
                        local = Mat4RotZXYTranslate(trans, rot);
                        if ((*params & 0x80) != 0) {
                            if (const auto dl = ArgU64(args, next)) dlPtr = *dl;
                            dlLayer = *params & 0x0F;
                        }
                        break;
                    }
                    case GeoOpcode::NodeTranslation:
                    case GeoOpcode::NodeBillboard: { // billboard treated as a translation
                        const auto params = ArgU8(args, 0);
                        Vec3s trans{};
                        if (const auto t = ArgVec3s(args, 1)) trans = *t;
                        local = Mat4RotZXYTranslate(trans, Vec3s());
                        if (params != nullptr && (*params & 0x80) != 0) {
                            if (const auto dl = ArgU64(args, 2)) dlPtr = *dl;
                            dlLayer = *params & 0x0F;
                        }
                        break;
                    }
                    case GeoOpcode::NodeRotation: {
                        const auto params = ArgU8(args, 0);
                        Vec3s rot{};
                        if (const auto r = ArgVec3s(args, 1)) rot = *r;
                        local = Mat4RotZXYTranslate(Vec3s(), rot);
                        if (params != nullptr && (*params & 0x80) != 0) {
                            if (const auto dl = ArgU64(args, 2)) dlPtr = *dl;
                            dlLayer = *params & 0x0F;
                        }
                        break;
                    }
                    case GeoOpcode::NodeScale: {
                        const auto params = ArgU8(args, 0);
                        if (const auto s = ArgU32(args, 1)) {
                            local = Mat4Scale((float)*s / 65536.0f);
                        }
                        if (params != nullptr && (*params & 0x80) != 0) {
                            if (const auto dl = ArgU64(args, 2)) dlPtr = *dl;
                            dlLayer = *params & 0x0F;
                        }
                        break;
                    }
                    case GeoOpcode::NodeAnimatedPart: { // bind pose: rest translation only
                        Vec3s trans{};
                        if (const auto layer = ArgU8(args, 0)) dlLayer = *layer & 0x0F;
                        if (const auto t = ArgVec3s(args, 1)) trans = *t;
                        local = Mat4RotZXYTranslate(trans, Vec3s());
                        if (const auto dl = ArgU64(args, 2)) dlPtr = *dl;
                        break;
                    }
                    case GeoOpcode::NodeDisplayList: {
                        if (const auto layer = ArgU8(args, 0)) dlLayer = *layer & 0x0F;
                        if (const auto dl = ArgU64(args, 1)) dlPtr = *dl;
                        break;
                    }
                    default:
                        break;
                }

                ctx.lastLocal = local;
                ctx.lastWasSwitch = cmd.opcode == GeoOpcode::NodeSwitchCase;

                if (dlPtr != 0) {
                    if (const auto dlName = ResolveAddr(dlPtr, *ctx.file)) {
                        const Mat4 world = Mat4Mul(local, ctx.stack.back());
                        UI::ModelPart part;
                        part.resource = dlName.value();
                        std::memcpy(part.mtx, world.m, sizeof(world.m));
                        part.layer = dlLayer;
                        ctx.parts.push_back(std::move(part));
                    }
                }
                break;
            }
        }
    }
}

const std::string* FindOwningFile(const ParseResultData& item) {
    for (const auto& [file, results] : Companion::Instance->GetParseResults()) {
        for (const auto& r : results) {
            if (&r == &item) {
                return &file;
            }
        }
    }
    return nullptr;
}

const std::vector<UI::ModelPart>& FlattenGeoLayout(const ParseResultData& item) {
    static std::unordered_map<std::string, std::vector<UI::ModelPart>> sCache;
    auto it = sCache.find(item.name);
    if (it != sCache.end()) {
        return it->second;
    }

    GeoWalk ctx;
    const std::string* file = FindOwningFile(item);
    if (file != nullptr && item.data.has_value()) {
        if (auto geo = std::static_pointer_cast<SM64::GeoLayout>(item.data.value())) {
            ctx.file = file;
            WalkGeoCommands(geo->commands, ctx);
        }
    }
    return sCache.emplace(item.name, std::move(ctx.parts)).first->second;
}

struct GeoModelView {
    float yaw = 0.6f;
    float pitch = 0.3f;
    float zoom = 1.0f;
};
std::unordered_map<std::string, GeoModelView> sGeoViews;

} // namespace

float SM64::GeoLayoutFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() * 2.0f + 324.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f;
}

void SM64::GeoLayoutFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);

    const auto& parts = FlattenGeoLayout(item);
    ImGui::TextDisabled("geo layout  \xe2\x80\x94  %zu parts, drag to orbit, \xe2\x8c\x98/Ctrl+scroll to zoom",
                        parts.size());

    GeoModelView& view = sGeoViews[item.name];

    const float vh = 320.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float vw = std::min(availW, vh * 3.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - vw) * 0.5f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##geoview", ImVec2(vw, vh));
    const float winTop = ImGui::GetWindowPos().y;
    const float winBot = winTop + ImGui::GetWindowHeight();
    const bool visible = ImGui::GetItemRectMax().y > winTop && ImGui::GetItemRectMin().y < winBot;
    if (ImGui::IsItemActive()) {
        const ImVec2 drag = ImGui::GetIO().MouseDelta;
        view.yaw -= drag.x * 0.01f;
        view.pitch = std::clamp(view.pitch + drag.y * 0.01f, -1.55f, 1.55f);
    }
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f && (io.KeyCtrl || io.KeySuper)) {
        view.zoom = std::clamp(view.zoom * (1.0f + io.MouseWheel * 0.1f), 0.02f, 50.0f);
        io.MouseWheel = 0.0f;
    }

    if (visible) {
        ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + vw, origin.y + vh),
                                                  IM_COL32(18, 18, 22, 255));
        if (parts.empty()) {
            const char* label = "no drawable display lists";
            const ImVec2 ts = ImGui::CalcTextSize(label);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(origin.x + (vw - ts.x) * 0.5f, origin.y + (vh - ts.y) * 0.5f), IM_COL32(120, 120, 130, 255),
                label);
        } else {
            UI::GetBackend()->DrawModelParts(item.name, parts, origin, ImVec2(vw, vh), view.yaw, view.pitch,
                                             view.zoom);
        }
    }
}
#endif // BUILD_UI
