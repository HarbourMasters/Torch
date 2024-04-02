#include "CollisionFactory.h"
#include "Companion.h"
#include "spdlog/spdlog.h"

#define FORMAT_HEX(x) std::hex << x << std::dec

std::unordered_map<int16_t, SpecialPresetTypes> specialPresetMap = {
    { 0x00, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x01, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x02, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x03, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x04, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x05, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x06, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x07, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x08, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x09, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x0A, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x0B, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x0C, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x0D, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x0E, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x0F, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x10, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x11, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x12, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x13, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x14, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x15, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x16, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x17, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x18, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x19, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x1A, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x1B, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x1C, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x1D, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x1E, SpecialPresetTypes::SPTYPE_UNKNOWN },
    { 0x1F, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x20, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x21, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x22, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x23, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x24, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x25, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x26, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x27, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x28, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x65, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x66, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x67, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x68, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x69, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x6A, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x6B, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x6C, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x6D, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x6E, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x6F, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x70, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x71, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x72, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x73, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x74, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x75, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x76, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x77, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x78, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x79, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x7A, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x7B, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x7C, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x7D, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
    { 0x89, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x7E, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x7F, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x80, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x81, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x82, SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS },
    { 0x8A, SpecialPresetTypes::SPTYPE_DEF_PARAM_AND_YROT },
    { 0x8B, SpecialPresetTypes::SPTYPE_DEF_PARAM_AND_YROT },
    { 0x8C, SpecialPresetTypes::SPTYPE_DEF_PARAM_AND_YROT },
    { 0x8D, SpecialPresetTypes::SPTYPE_DEF_PARAM_AND_YROT },
    { 0x88, SpecialPresetTypes::SPTYPE_PARAMS_AND_YROT },
    { 0x83, SpecialPresetTypes::SPTYPE_PARAMS_AND_YROT },
    { 0x84, SpecialPresetTypes::SPTYPE_PARAMS_AND_YROT },
    { 0x85, SpecialPresetTypes::SPTYPE_PARAMS_AND_YROT },
    { 0x86, SpecialPresetTypes::SPTYPE_PARAMS_AND_YROT },
    { 0x87, SpecialPresetTypes::SPTYPE_PARAMS_AND_YROT },
    { 0xFF, SpecialPresetTypes::SPTYPE_NO_YROT_OR_PARAMS },
};

ExportResult SM64::CollisionCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto symbol = GetSafeNode(node, "symbol", entryName);
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto collision = std::static_pointer_cast<Collision>(data).get();
    uint32_t count = 0;

    write << "Collision " << symbol << "[] = {\n";

    write << fourSpaceTab;

    write << "COL_INIT(),\n";
    ++count;

    // Vertices
    if (collision->mVertices.size() > 0) {
        write << fourSpaceTab;
        write << "COL_VERTEX_INIT(" << FORMAT_HEX(collision->mVertices.size()) << "),\n";
        ++count;
    }
    for (auto &vertex : collision->mVertices) {
        write << fourSpaceTab;
        write << "COL_VERTEX(";
        write << vertex.x << ", ";
        write << vertex.y << ", ";
        write << vertex.z << "),\n";
        count += 3;
    }

    // Surfaces
    for (auto &surface : collision->mSurfaces) {
        // size check is probably not necessary here
        if (surface.tris.size() > 0) {
            write << fourSpaceTab;
            write << "COL_TRI_INIT(";
            write << surface.surfaceType << ", ";
            write << surface.tris.size() << "),\n";
            count += 2;
        }
        bool hasForce = false;
        switch (surface.surfaceType) {
                case SurfaceType::SURFACE_0004:
                case SurfaceType::SURFACE_FLOWING_WATER:
                case SurfaceType::SURFACE_DEEP_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_SHALLOW_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_HORIZONTAL_WIND:
                case SurfaceType::SURFACE_INSTANT_MOVING_QUICKSAND:
                    hasForce = true;
                    break;
                default:
                    break;
            }
        for (auto &tri : surface.tris) {
            write << fourSpaceTab;
            if (hasForce) {
                write << "COL_TRI_SPECIAL(";
            } else {
                write << "COL_TRI(";
            }

            write << tri.x << ", ";
            write << tri.y << ", ";
            if (hasForce) {
                write << tri.z << ", ";
                write << tri.force << "),\n";
                count += 4;
            } else {
                write << tri.z << "),\n";
                count += 3;
            }
        }
    }
    if (collision->mSurfaces.size()) {
        write << fourSpaceTab;
        write << "COL_TRI_STOP()";
        ++count;
    }

    // Special Objects
    if (collision->mSpecialObjects.size() > 0) {
        write << fourSpaceTab;
        write << "COL_SPECIAL_INIT(" << collision->mSpecialObjects.size() << "),\n";
        count += 2;
    }
    for (auto &specialObject : collision->mSpecialObjects) {
        write << fourSpaceTab;
        if (specialPresetMap.find((int16_t)specialObject.presetId) == specialPresetMap.end()) {
            throw std::runtime_error("Special Preset Id has no associated Type");
        }
        SpecialPresetTypes type = specialPresetMap.at((int16_t)specialObject.presetId);
        switch (type) {
            case SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS:
            case SpecialPresetTypes::SPTYPE_DEF_PARAM_AND_YROT:
                write << "SPECIAL_OBJECT_WITH_YAW(";
                break;
            case SpecialPresetTypes::SPTYPE_PARAMS_AND_YROT:
                write << "SPECIAL_OBJECT_WITH_YAW_AND_PARAM(";
                break;
            case SpecialPresetTypes::SPTYPE_UNKNOWN:
                break;
            default:
                write << "SPECIAL_OBJECT(";
                break;
        }
        write << specialObject.presetId << ", ";
        write << specialObject.x << ", ";
        write << specialObject.y << ", ";
        write << specialObject.z;
        count += 4;

        for (int16_t extraParam : specialObject.extraParams) {
            write << ", ";
            write << extraParam;
            ++count;
        }

        if (type != SpecialPresetTypes::SPTYPE_UNKNOWN) {
            write << ")";
        }
        write << ",\n";
    }

    // Environment Region Boxes
    if (collision->mEnvRegionBoxes.size() > 0) {
        write << fourSpaceTab;
        write << "COL_WATER_BOX_INIT(" << collision->mEnvRegionBoxes.size() << "),\n";
        count += 2;
    }
    for (auto &envRegionBox : collision->mEnvRegionBoxes) {
        write << fourSpaceTab;
        write << envRegionBox.id << ", ";
        write << envRegionBox.x1 << ", ";
        write << envRegionBox.z1 << ", ";
        write << envRegionBox.x2 << ", ";
        write << envRegionBox.z2 << ", ";
        write << envRegionBox.height << "),\n";
        count += 6;
    }

    if (Companion::Instance->IsDebug()) {
        write << "// size: 0x" << std::hex << std::uppercase << "" << "\n";
    }

    return offset + count * sizeof(int16_t);
}

ExportResult SM64::CollisionHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern " << "Collision " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult SM64::CollisionBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto collision = std::static_pointer_cast<Collision>(data).get();

    auto writer = LUS::BinaryWriter();
    writer.SetEndianness(LUS::Endianness::Big);

    writer.Write((int16_t)COL_INIT());

    // Vertices
    if (collision->mVertices.size() > 0) {
        writer.Write((int16_t)COL_VERTEX_INIT(collision->mVertices.size()));
    }
    for (auto &vertex : collision->mVertices) {
        writer.Write(vertex.x);
        writer.Write(vertex.y);
        writer.Write(vertex.z);
    }

    // Surfaces
    for (auto &surface : collision->mSurfaces) {
        // size check is probably not necessary here
        if (surface.tris.size() > 0) {
            writer.Write((int16_t)surface.surfaceType);
            writer.Write((int16_t)surface.tris.size());
        }
        bool hasForce = false;
        switch (surface.surfaceType) {
            case SurfaceType::SURFACE_0004:
            case SurfaceType::SURFACE_FLOWING_WATER:
            case SurfaceType::SURFACE_DEEP_MOVING_QUICKSAND:
            case SurfaceType::SURFACE_SHALLOW_MOVING_QUICKSAND:
            case SurfaceType::SURFACE_MOVING_QUICKSAND:
            case SurfaceType::SURFACE_HORIZONTAL_WIND:
            case SurfaceType::SURFACE_INSTANT_MOVING_QUICKSAND:
                hasForce = true;
                break;
            default:
                break;
        }
        for (auto &tri : surface.tris) {
            writer.Write(tri.x);
            writer.Write(tri.y);
            writer.Write(tri.z);
            if (hasForce) {
                writer.Write(tri.force);
            }
        }
    }
    if (collision->mSurfaces.size()) {
        writer.Write((int16_t)COL_TRI_STOP());
    }

    // Special Objects
    if (collision->mSpecialObjects.size() > 0) {
        writer.Write((int16_t)TERRAIN_LOAD_OBJECTS);
        writer.Write((int16_t)collision->mSpecialObjects.size());
    }
    for (auto &specialObject : collision->mSpecialObjects) {
        writer.Write((int16_t)specialObject.presetId);
        writer.Write(specialObject.x);
        writer.Write(specialObject.y);
        writer.Write(specialObject.z);
        for (int16_t extraParam : specialObject.extraParams) {
            writer.Write(extraParam);
        }
    }

    // Environment Region Boxes
    if (collision->mEnvRegionBoxes.size() > 0) {
        writer.Write((int16_t)TERRAIN_LOAD_ENVIRONMENT);
        writer.Write((int16_t)collision->mEnvRegionBoxes.size());
    }
    for (auto &envRegionBox : collision->mEnvRegionBoxes) {
        writer.Write(envRegionBox.id);
        writer.Write(envRegionBox.x1);
        writer.Write(envRegionBox.z1);
        writer.Write(envRegionBox.x2);
        writer.Write(envRegionBox.z2);
        writer.Write(envRegionBox.height);
    }

    std::vector<char> buffer = writer.ToVector();
    writer.Close();

    LUS::BinaryWriter output = LUS::BinaryWriter();
    WriteHeader(output, LUS::ResourceType::Blob, 0);

    output.Write(static_cast<uint32_t>(buffer.size()));
    output.Write(buffer.data(), buffer.size());
    output.Finish(write);
    output.Close();
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SM64::CollisionFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    std::vector<CollisionVertex> vertices;
    std::vector<CollisionSurface> surfaces;
    std::vector<SpecialObject> specialObjects;
    std::vector<EnvRegionBox> envRegionBoxes;
    bool processing = true;
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    auto cmd = segment.data;
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    while (processing) {
        int16_t terrainLoadType = reader.ReadInt16();

        if (TERRAIN_LOAD_IS_SURFACE_TYPE_LOW(terrainLoadType)) {
            SurfaceType surfaceType = static_cast<SurfaceType>(terrainLoadType);
            bool hasForce = false;
            switch (surfaceType) {
                case SurfaceType::SURFACE_0004:
                case SurfaceType::SURFACE_FLOWING_WATER:
                case SurfaceType::SURFACE_DEEP_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_SHALLOW_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_HORIZONTAL_WIND:
                case SurfaceType::SURFACE_INSTANT_MOVING_QUICKSAND:
                    hasForce = true;
                    break;
                default:
                    break;
            }
            int16_t numSurfaces = reader.ReadInt16();
            std::vector<CollisionTri> tris;
            for (int16_t i = 0; i < numSurfaces; ++i) {
                int16_t x = reader.ReadInt16();
                int16_t y = reader.ReadInt16();
                int16_t z = reader.ReadInt16();
                int16_t force = hasForce ? reader.ReadInt16() : -1;
                tris.emplace_back(x, y, z, force);
            }
            surfaces.emplace_back(surfaceType, tris);
        } else if (terrainLoadType == TERRAIN_LOAD_VERTICES) {
            int16_t numVertices = reader.ReadInt16();
            for (int16_t i = 0; i < numVertices; ++i) {
                int16_t x = reader.ReadInt16();
                int16_t y = reader.ReadInt16();
                int16_t z = reader.ReadInt16();
                vertices.emplace_back(x, y, z);
            }
        } else if (terrainLoadType == TERRAIN_LOAD_OBJECTS) {
            int16_t numSpecialObjects = reader.ReadInt16();
            for (int16_t i = 0; i < numSpecialObjects; ++i) {
                int16_t presetId = reader.ReadInt16();
                int16_t x = reader.ReadInt16();
                int16_t y = reader.ReadInt16();
                int16_t z = reader.ReadInt16();
                if (specialPresetMap.find(presetId) == specialPresetMap.end()) {
                    throw std::runtime_error("Special Preset Id has no associated Type");
                }
                SpecialPresets specialPresetId = static_cast<SpecialPresets>(presetId);
                SpecialPresetTypes type = specialPresetMap.at(presetId);
                std::vector<int16_t> extraParams;
                switch (type) {
                    case SpecialPresetTypes::SPTYPE_YROT_NO_PARAMS:
                        extraParams.emplace_back(reader.ReadInt16());
                    case SpecialPresetTypes::SPTYPE_PARAMS_AND_YROT:
                        extraParams.emplace_back(reader.ReadInt16());
                        extraParams.emplace_back(reader.ReadInt16());
                    case SpecialPresetTypes::SPTYPE_UNKNOWN:
                        extraParams.emplace_back(reader.ReadInt16());
                        extraParams.emplace_back(reader.ReadInt16());
                        extraParams.emplace_back(reader.ReadInt16());
                        break;
                    case SpecialPresetTypes::SPTYPE_DEF_PARAM_AND_YROT:
                        extraParams.emplace_back(reader.ReadInt16());
                        break;
                    default:
                        break;
                }
                specialObjects.emplace_back(specialPresetId, x, y, z, extraParams);
            }
        } else if (terrainLoadType == TERRAIN_LOAD_ENVIRONMENT) {
            int16_t numRegions = reader.ReadInt16();
            for (int16_t i = 0; i < numRegions; ++i) {
                int16_t id = reader.ReadInt16();
                int16_t x1 = reader.ReadInt16();
                int16_t z1 = reader.ReadInt16();
                int16_t x2 = reader.ReadInt16();
                int16_t z2 = reader.ReadInt16();
                int16_t height = reader.ReadInt16();
                envRegionBoxes.emplace_back(id, x1, z1, x2, z2, height);
            }
        } else if (terrainLoadType == TERRAIN_LOAD_CONTINUE) {
            // need to figure out a way to handle when this should appear in exporters. seems to always be after vertices
        } else if (terrainLoadType == TERRAIN_LOAD_END) {
            processing = false;
        } else if (TERRAIN_LOAD_IS_SURFACE_TYPE_HIGH(terrainLoadType)) {
            SurfaceType surfaceType = static_cast<SurfaceType>(terrainLoadType);
            bool hasForce = false;
            switch (surfaceType) {
                case SurfaceType::SURFACE_0004:
                case SurfaceType::SURFACE_FLOWING_WATER:
                case SurfaceType::SURFACE_DEEP_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_SHALLOW_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_MOVING_QUICKSAND:
                case SurfaceType::SURFACE_HORIZONTAL_WIND:
                case SurfaceType::SURFACE_INSTANT_MOVING_QUICKSAND:
                    hasForce = true;
                    break;
                default:
                    break;
            }
            int16_t numSurfaces = reader.ReadInt16();
            std::vector<CollisionTri> tris;
            for (int16_t i = 0; i < numSurfaces; ++i) {
                int16_t x = reader.ReadInt16();
                int16_t y = reader.ReadInt16();
                int16_t z = reader.ReadInt16();
                int16_t force = hasForce ? reader.ReadInt16() : -1;
                tris.emplace_back(x, y, z, force);
            }
            surfaces.emplace_back(surfaceType, tris);
        }
    }

    return std::make_shared<Collision>(vertices, surfaces, specialObjects, envRegionBoxes);
}