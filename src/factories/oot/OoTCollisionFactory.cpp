#ifdef OOT_SUPPORT

#include "OoTCollisionFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"

namespace OoT {

std::optional<std::shared_ptr<IParsedData>> OoTCollisionFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // CollisionHeader: 44 bytes (0x2C)
    auto [_, segment] = Decompressor::AutoDecode(node, buffer, 0x2C);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);

    auto col = std::make_shared<OoTCollisionData>();

    // Bounding box
    col->absMinX = reader.ReadInt16();
    col->absMinY = reader.ReadInt16();
    col->absMinZ = reader.ReadInt16();
    col->absMaxX = reader.ReadInt16();
    col->absMaxY = reader.ReadInt16();
    col->absMaxZ = reader.ReadInt16();

    uint16_t numVerts = reader.ReadUInt16();
    reader.ReadInt16(); // padding
    uint32_t vtxAddr = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());

    uint16_t numPolygons = reader.ReadUInt16();
    reader.ReadInt16(); // padding
    uint32_t polyAddr = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());
    uint32_t polyTypeDefAddr = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());
    uint32_t camDataAddr = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());

    uint16_t numWaterBoxes = reader.ReadUInt16();
    reader.ReadInt16(); // padding
    uint32_t waterBoxAddr = Companion::Instance->PatchVirtualAddr(reader.ReadUInt32());

    // Read vertices
    if (numVerts > 0 && vtxAddr != 0) {
        YAML::Node vNode;
        vNode["offset"] = vtxAddr;
        auto vRaw = Decompressor::AutoDecode(vNode, buffer, numVerts * 6);
        LUS::BinaryReader vReader(vRaw.segment.data, vRaw.segment.size);
        vReader.SetEndianness(Torch::Endianness::Big);

        for (uint16_t i = 0; i < numVerts; i++) {
            CollisionVertex v;
            v.x = vReader.ReadInt16();
            v.y = vReader.ReadInt16();
            v.z = vReader.ReadInt16();
            col->vertices.push_back(v);
        }
    }

    // Read polygons
    if (numPolygons > 0 && polyAddr != 0) {
        YAML::Node pNode;
        pNode["offset"] = polyAddr;
        auto pRaw = Decompressor::AutoDecode(pNode, buffer, numPolygons * 16);
        LUS::BinaryReader pReader(pRaw.segment.data, pRaw.segment.size);
        pReader.SetEndianness(Torch::Endianness::Big);

        for (uint16_t i = 0; i < numPolygons; i++) {
            CollisionPoly p;
            p.type = pReader.ReadUInt16();
            p.vtxA = pReader.ReadUInt16();
            p.vtxB = pReader.ReadUInt16();
            p.vtxC = pReader.ReadUInt16();
            p.normX = pReader.ReadUInt16();
            p.normY = pReader.ReadUInt16();
            p.normZ = pReader.ReadUInt16();
            p.dist = pReader.ReadUInt16();
            col->polygons.push_back(p);
        }
    }

    // Read surface types: count = highest polygon type + 1
    if (polyTypeDefAddr != 0 && !col->polygons.empty()) {
        uint16_t highestType = 0;
        for (const auto& p : col->polygons) {
            if (p.type > highestType) highestType = p.type;
        }
        uint32_t numSurfaceTypes = highestType + 1;

        YAML::Node stNode;
        stNode["offset"] = polyTypeDefAddr;
        auto stRaw = Decompressor::AutoDecode(stNode, buffer, numSurfaceTypes * 8);
        LUS::BinaryReader stReader(stRaw.segment.data, stRaw.segment.size);
        stReader.SetEndianness(Torch::Endianness::Big);

        for (uint32_t i = 0; i < numSurfaceTypes; i++) {
            SurfaceType st;
            st.data0 = stReader.ReadUInt32();
            st.data1 = stReader.ReadUInt32();
            col->surfaceTypes.push_back(st);
        }
    }

    // Read camera data
    // Uses segment offsets throughout to match OTRExporter's logic.
    if (camDataAddr != 0) {
        uint32_t camDataSegOff = SEGMENT_OFFSET(camDataAddr);
        uint8_t sceneSeg = SEGMENT_NUMBER(camDataAddr);

        // Determine upper boundary for camera data entries (in segment offsets)
        uint32_t upperBoundary = 0;
        if (polyTypeDefAddr != 0) {
            upperBoundary = SEGMENT_OFFSET(polyTypeDefAddr);
        } else if (polyAddr != 0) {
            upperBoundary = SEGMENT_OFFSET(polyAddr);
        } else if (vtxAddr != 0) {
            upperBoundary = SEGMENT_OFFSET(vtxAddr);
        } else if (waterBoxAddr != 0) {
            upperBoundary = SEGMENT_OFFSET(waterBoxAddr);
        } else {
            upperBoundary = SEGMENT_OFFSET(GetSafeNode<uint32_t>(node, "offset"));
        }

        // Initial Sharp Ocarina check: cam data entries come before the boundary
        // in standard layout. If boundary < camDataSegOff, layout is reversed.
        bool isSharpOcarina = false;
        if (upperBoundary < camDataSegOff) {
            uint32_t scanSize = 0x2000;
            YAML::Node scanNode;
            scanNode["offset"] = camDataAddr;
            auto scanRaw = Decompressor::AutoDecode(scanNode, buffer, scanSize);

            uint32_t offset = 0;
            while (offset + 8 <= scanSize &&
                   scanRaw.segment.data[offset] == 0x00 &&
                   scanRaw.segment.data[offset + 4] == 0x02) {
                offset += 8;
            }
            upperBoundary = camDataSegOff + offset;
            isSharpOcarina = true;
        }

        uint32_t numEntries = (upperBoundary - camDataSegOff) / 8;
        if (numEntries > 0 && numEntries < 10000) {
            YAML::Node cdNode;
            cdNode["offset"] = camDataAddr;
            auto cdRaw = Decompressor::AutoDecode(cdNode, buffer, numEntries * 8);
            LUS::BinaryReader cdReader(cdRaw.segment.data, cdRaw.segment.size);
            cdReader.SetEndianness(Torch::Endianness::Big);

            // Match OTRExporter's per-entry Sharp Ocarina detection:
            // For each entry, if the position pointer's segment offset >= camDataSegOff,
            // it's Sharp Ocarina layout. This also triggers when cameraPosDataSeg == 0
            // and camDataSegOff == 0, since 0 >= 0 (the case for object collisions
            // where cam data is at the start of the file).
            uint32_t lowestCamPosOffset = camDataSegOff;
            uint32_t highestCamPosEnd = camDataSegOff;

            for (uint32_t i = 0; i < numEntries; i++) {
                CamDataEntry entry;
                entry.cameraSType = cdReader.ReadUInt16();
                entry.numData = cdReader.ReadInt16();
                uint32_t cameraPosDataSeg = cdReader.ReadUInt32();
                entry.cameraPosIndex = 0;

                uint32_t posSegOffset = SEGMENT_OFFSET(cameraPosDataSeg);

                if (camDataSegOff > posSegOffset) {
                    // Standard layout: positions are before cam data entries
                    if (cameraPosDataSeg != 0 && posSegOffset < lowestCamPosOffset) {
                        lowestCamPosOffset = posSegOffset;
                    }
                } else {
                    // Sharp Ocarina layout: positions are after cam data entries
                    isSharpOcarina = true;
                    if (highestCamPosEnd < posSegOffset) {
                        highestCamPosEnd = posSegOffset;
                    }
                }

                col->camDataEntries.push_back(entry);
            }

            // Calculate camera position data count and offset (in segment offsets)
            uint32_t camPosDataSegOff;
            uint32_t numPosData;
            if (!isSharpOcarina) {
                camPosDataSegOff = lowestCamPosOffset;
                numPosData = (camDataSegOff - camPosDataSegOff) / 6;
            } else {
                camPosDataSegOff = camDataSegOff + numEntries * 8;
                numPosData = (highestCamPosEnd - camPosDataSegOff + 18) / 6;
            }

            // Read camera position data
            if (numPosData > 0 && numPosData < 100000) {
                uint32_t camPosSeg = (sceneSeg << 24) | camPosDataSegOff;
                YAML::Node cpNode;
                cpNode["offset"] = camPosSeg;
                auto cpRaw = Decompressor::AutoDecode(cpNode, buffer, numPosData * 6);
                LUS::BinaryReader cpReader(cpRaw.segment.data, cpRaw.segment.size);
                cpReader.SetEndianness(Torch::Endianness::Big);

                for (uint32_t i = 0; i < numPosData; i++) {
                    CamPosData pos;
                    pos.x = cpReader.ReadInt16();
                    pos.y = cpReader.ReadInt16();
                    pos.z = cpReader.ReadInt16();
                    col->camPositions.push_back(pos);
                }
            }

            // Re-read entries to set camera position indices
            cdReader.Seek(0, LUS::SeekOffsetType::Start);
            for (uint32_t i = 0; i < numEntries; i++) {
                cdReader.ReadUInt16(); // skip cameraSType
                cdReader.ReadInt16();  // skip numData
                uint32_t cameraPosDataSeg = cdReader.ReadUInt32();

                if (cameraPosDataSeg != 0) {
                    uint32_t posSegOffset = SEGMENT_OFFSET(cameraPosDataSeg);
                    col->camDataEntries[i].cameraPosIndex = (posSegOffset - camPosDataSegOff) / 6;
                }
            }
        }
    }

    // Read water boxes
    if (numWaterBoxes > 0 && waterBoxAddr != 0) {
        YAML::Node wbNode;
        wbNode["offset"] = waterBoxAddr;
        auto wbRaw = Decompressor::AutoDecode(wbNode, buffer, numWaterBoxes * 16);
        LUS::BinaryReader wbReader(wbRaw.segment.data, wbRaw.segment.size);
        wbReader.SetEndianness(Torch::Endianness::Big);

        for (uint16_t i = 0; i < numWaterBoxes; i++) {
            WaterBox wb;
            wb.xMin = wbReader.ReadInt16();
            wb.ySurface = wbReader.ReadInt16();
            wb.zMin = wbReader.ReadInt16();
            wb.xLength = wbReader.ReadInt16();
            wb.zLength = wbReader.ReadInt16();
            wbReader.ReadInt16(); // padding
            wb.properties = wbReader.ReadUInt32();
            col->waterBoxes.push_back(wb);
        }
    }

    return col;
}

ExportResult OoTCollisionBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                 std::string& entryName, YAML::Node& node,
                                                 std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto col = std::static_pointer_cast<OoTCollisionData>(raw);

    WriteHeader(writer, Torch::ResourceType::OoTCollisionHeader, 0);

    // Bounding box
    writer.Write(col->absMinX);
    writer.Write(col->absMinY);
    writer.Write(col->absMinZ);
    writer.Write(col->absMaxX);
    writer.Write(col->absMaxY);
    writer.Write(col->absMaxZ);

    // Vertices
    writer.Write(static_cast<uint32_t>(col->vertices.size()));
    for (auto& v : col->vertices) {
        writer.Write(v.x);
        writer.Write(v.y);
        writer.Write(v.z);
    }

    // Polygons
    writer.Write(static_cast<uint32_t>(col->polygons.size()));
    for (auto& p : col->polygons) {
        writer.Write(p.type);
        writer.Write(p.vtxA);
        writer.Write(p.vtxB);
        writer.Write(p.vtxC);
        writer.Write(p.normX);
        writer.Write(p.normY);
        writer.Write(p.normZ);
        writer.Write(p.dist);
    }

    // Surface types (written in reversed data order to match OTRExporter)
    writer.Write(static_cast<uint32_t>(col->surfaceTypes.size()));
    for (auto& st : col->surfaceTypes) {
        writer.Write(st.data1);
        writer.Write(st.data0);
    }

    // Camera data entries
    writer.Write(static_cast<uint32_t>(col->camDataEntries.size()));
    for (auto& entry : col->camDataEntries) {
        writer.Write(entry.cameraSType);
        writer.Write(entry.numData);
        writer.Write(entry.cameraPosIndex);
    }

    // Camera positions
    writer.Write(static_cast<uint32_t>(col->camPositions.size()));
    for (auto& pos : col->camPositions) {
        writer.Write(pos.x);
        writer.Write(pos.y);
        writer.Write(pos.z);
    }

    // Water boxes
    writer.Write(static_cast<uint32_t>(col->waterBoxes.size()));
    for (auto& wb : col->waterBoxes) {
        writer.Write(wb.xMin);
        writer.Write(wb.ySurface);
        writer.Write(wb.zMin);
        writer.Write(wb.xLength);
        writer.Write(wb.zLength);
        writer.Write(wb.properties);
    }

    writer.Finish(write);
    return std::nullopt;
}

} // namespace OoT

#endif
