#include "ModelFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define BK64_MODEL_HEADER 0xB
#define TEXTURE_HEADER_SIZE 0x8
#define TEXTURE_METADATA_SIZE 0x10
#define GFX_HEADER_SIZE 0x8
#define GFX_CMD_SIZE 0x8
#define VTX_HEADER_SIZE 0x18
#define ANIM_TEXTURE_LIST_COUNT 4

namespace BK64 {

static const std::unordered_map<std::string, uint8_t> gF3DTable = {
    { "G_VTX", 0x04 },     { "G_DL", 0x06 },      { "G_MTX", 0x1 },    { "G_ENDDL", 0xB8 },
    { "G_SETTIMG", 0xFD }, { "G_MOVEMEM", 0x03 }, { "G_MV_L0", 0x86 }, { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA }, { "G_TRI2", 0xB1 },    { "G_QUAD", -1 }
};

static const std::unordered_map<std::string, uint8_t> gF3DExTable = {
    { "G_VTX", 0x04 },     { "G_DL", 0x06 },      { "G_MTX", 0x1 },    { "G_ENDDL", 0xB8 },
    { "G_SETTIMG", 0xFD }, { "G_MOVEMEM", 0x03 }, { "G_MV_L0", 0x86 }, { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA }, { "G_TRI2", 0xB1 },    { "G_QUAD", 0xB5 }
};

static const std::unordered_map<std::string, uint8_t> gF3DEx2Table = {
    { "G_VTX", 0x01 },     { "G_DL", 0xDE },      { "G_MTX", 0xDA },   { "G_ENDDL", 0xDF },
    { "G_SETTIMG", 0xFD }, { "G_MOVEMEM", 0xDC }, { "G_MV_L0", 0x86 }, { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA }, { "G_TRI2", 0x06 },    { "G_QUAD", 0x07 }
};

static const std::unordered_map<GBIVersion, std::unordered_map<std::string, uint8_t>> gGBITable = {
    { GBIVersion::f3d, gF3DTable },
    { GBIVersion::f3dex, gF3DExTable },
    { GBIVersion::f3dex2, gF3DEx2Table },
};

#define GBI(cmd) gGBITable.at(Companion::Instance->GetGBIVersion()).at(#cmd)

ExportResult ModelHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto model = std::static_pointer_cast<ModelData>(raw);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern BKModelHeader " << symbol << "_Header;\n";

    if (model->mHasAnimation && !model->mBones.empty()) {
        write << "extern BKAnimHeader " << symbol << "_AnimHeader;\n";
        write << "extern BKBone " << symbol << "_Bones[];\n";
    }

    if (model->mHasCollision) {
        write << "extern BKCollisionHeader " << symbol << "_CollisionHeader;\n";
        if (!model->mGeoCubes.empty()) {
            write << "extern BKGeoCube " << symbol << "_GeoCubes[];\n";
        }
        if (!model->mCollisionTris.empty()) {
            write << "extern BKCollisionTri " << symbol << "_CollisionTris[];\n";
        }
    }

    if (!model->mEffects.empty()) {
        write << "extern BKEffect " << symbol << "_Effects[];\n";
    }

    if (!model->mAnimTextures.empty()) {
        write << "extern BKAnimTexture " << symbol << "_AnimTextures[];\n";
    }

    return std::nullopt;
}

ExportResult ModelCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                       YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto model = std::static_pointer_cast<ModelData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    // Header
    write << "BKModelHeader " << symbol << "_Header = {\n";
    write << fourSpaceTab << "/* geoType */ " << model->mGeoType << ",\n";
    write << fourSpaceTab << "/* triCount */ " << model->mTriCount << ",\n";
    write << fourSpaceTab << "/* vertCount */ " << model->mVertCount << "\n";
    write << "};\n\n";

    // Animation, if the model has any
    if (model->mHasAnimation && !model->mBones.empty()) {
        write << "BKAnimHeader " << symbol << "_AnimHeader = {\n";
        write << fourSpaceTab << "/* scalingFactor */ " << model->mAnimHeader.scalingFactor << "f,\n";
        write << fourSpaceTab << "/* boneCount */ " << model->mBones.size() << "\n";
        write << "};\n\n";

        write << "BKBone " << symbol << "_Bones[] = {\n";
        for (const auto& bone : model->mBones) {
            write << fourSpaceTab << "{ ";
            write << bone.pos[0] << "f, " << bone.pos[1] << "f, " << bone.pos[2] << "f, ";
            write << bone.id << ", " << bone.parentId;
            write << " },\n";
        }
        write << "};\n\n";
    }

    // Collision
    if (model->mHasCollision) {
        write << "BKCollisionHeader " << symbol << "_CollisionHeader = {\n";
        write << fourSpaceTab << "/* minIndex */ { " << model->mCollisionHeader.minIndex[0] << ", "
              << model->mCollisionHeader.minIndex[1] << ", " << model->mCollisionHeader.minIndex[2] << " },\n";
        write << fourSpaceTab << "/* maxIndex */ { " << model->mCollisionHeader.maxIndex[0] << ", "
              << model->mCollisionHeader.maxIndex[1] << ", " << model->mCollisionHeader.maxIndex[2] << " },\n";
        write << fourSpaceTab << "/* yStride */ " << model->mCollisionHeader.yStride << ",\n";
        write << fourSpaceTab << "/* zStride */ " << model->mCollisionHeader.zStride << ",\n";
        write << fourSpaceTab << "/* geoCubeScale */ " << model->mCollisionHeader.geoCubeScale << ",\n";
        write << fourSpaceTab << "/* geoCubeCount */ " << model->mGeoCubes.size() << ",\n";
        write << fourSpaceTab << "/* triCount */ " << model->mCollisionTris.size() << "\n";
        write << "};\n\n";

        if (!model->mGeoCubes.empty()) {
            write << "BKGeoCube " << symbol << "_GeoCubes[] = {\n";
            for (const auto& cube : model->mGeoCubes) {
                write << fourSpaceTab << "{ " << cube.startTri << ", " << cube.triCount << " },\n";
            }
            write << "};\n\n";
        }

        if (!model->mCollisionTris.empty()) {
            write << "BKCollisionTri " << symbol << "_CollisionTris[] = {\n";
            for (const auto& tri : model->mCollisionTris) {
                write << fourSpaceTab << "{ ";
                write << "{ " << tri.vtxIds[0] << ", " << tri.vtxIds[1] << ", " << tri.vtxIds[2] << " }, ";
                write << tri.unk6 << ", " << std::hex << "0x" << tri.flags << std::dec;
                write << " },\n";
            }
            write << "};\n\n";
        }
    }

    // Effects
    if (!model->mEffects.empty()) {
        write << "BKEffect " << symbol << "_Effects[] = {\n";
        for (const auto& effect : model->mEffects) {
            write << fourSpaceTab << "{ " << effect.dataInfo << ", ";
            write << effect.vtxIndices.size() << ", { ";
            for (size_t i = 0; i < effect.vtxIndices.size(); i++) {
                write << effect.vtxIndices[i];
                if (i < effect.vtxIndices.size() - 1)
                    write << ", ";
            }
            write << " } },\n";
        }
        write << "};\n\n";
    }

    // Animated textures
    if (!model->mAnimTextures.empty()) {
        write << "BKAnimTexture " << symbol << "_AnimTextures[] = {\n";
        for (const auto& animTex : model->mAnimTextures) {
            write << fourSpaceTab << "{ ";
            write << animTex.frameSize << ", " << animTex.frameCount << ", ";
            write << animTex.frameRate << "f";
            write << " },\n";
        }
        write << "};\n\n";
    }

    return offset;
}

ExportResult BK64::ModelBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto model = std::static_pointer_cast<ModelData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKModel, 0);

    // ── Core ──────────────────────────────────────────────────────────────────
    writer.Write(model->mGeoType);
    writer.Write(model->mTriCount);
    writer.Write(model->mVertCount);

    // ── Presence flags ────────────────────────────────────────────────────────
    writer.Write(static_cast<uint8_t>(model->mHasGeo ? 1 : 0));
    writer.Write(static_cast<uint8_t>(model->mHasVtx ? 1 : 0));
    writer.Write(static_cast<uint8_t>(model->mHasDL ? 1 : 0));
    writer.Write(static_cast<uint16_t>(model->mTexInfos.size()));
    writer.Write(static_cast<uint8_t>(model->mHasAnimation ? 1 : 0));
    writer.Write(static_cast<uint8_t>(model->mHasCollision ? 1 : 0));
    writer.Write(static_cast<uint8_t>(model->mHasUnk14 ? 1 : 0));
    writer.Write(static_cast<uint8_t>(model->mHasUnk20 ? 1 : 0));
    writer.Write(static_cast<uint8_t>(!model->mEffects.empty() ? 1 : 0));
    writer.Write(static_cast<uint8_t>(model->mHasUnk28 ? 1 : 0));
    writer.Write(static_cast<uint8_t>(!model->mAnimTextures.empty() ? 1 : 0));

    // ── VTX header ────────────────────────────────────────────────────────────
    if (model->mHasVtx) {
        const auto& vh = model->mVtxHeader;
        writer.Write(vh.minCoord[0]);
        writer.Write(vh.minCoord[1]);
        writer.Write(vh.minCoord[2]);
        writer.Write(vh.maxCoord[0]);
        writer.Write(vh.maxCoord[1]);
        writer.Write(vh.maxCoord[2]);
        writer.Write(vh.centerCoord[0]);
        writer.Write(vh.centerCoord[1]);
        writer.Write(vh.centerCoord[2]);
        writer.Write(vh.localNorm);
        writer.Write(vh.count);
        writer.Write(vh.globalNorm);
    }

    // ── GFX / display-list info ───────────────────────────────────────────────
    if (model->mHasDL) {
        writer.Write(model->mDLCount);
        writer.Write(model->mDLUnkInfo);
        writer.Write(model->mGfxSubListCount);

        // Build a lookup from each static texture's IMAGE segment-2 offset back to its texture
        // index. CI4/CI8 put the palette at textureDataOffset and the actual image (the _tex_<i>
        // resource) right after it at + tlutColors*2; everything else has the image at the offset.
        std::unordered_map<uint32_t, uint32_t> imageOffsetToTex;
        for (uint32_t ti = 0; ti < model->mTexInfos.size(); ti++) {
            const auto& tex = model->mTexInfos[ti];
            const bool isCI = tex.type == 0x1 || tex.type == 0x2; // CI4 / CI8
            const uint32_t tlutByteSize = isCI ? tex.tlutColors * 2u : 0u;
            imageOffsetToTex[tex.textureDataOffset + tlutByteSize] = ti;
        }

        // The falling jiggies transition rewrites its own texture at runtime, so leave its
        // G_SETTIMG alone. Touch it and you get the white fallback texture instead.
        const bool isFramebufferSubstitutionModel = entryName.find("TRANSITION_FALLING_JIGGIES") != std::string::npos;
        for (size_t i = 0; i + 1 < model->mRawDLWords.size(); i += 2) {
            uint32_t w0 = model->mRawDLWords[i];
            uint32_t w1 = model->mRawDLWords[i + 1];
            if (!isFramebufferSubstitutionModel && (w0 >> 24) == 0xFD /* G_SETTIMG */ &&
                SEGMENT_NUMBER(w1) == 2) {
                auto it = imageOffsetToTex.find(SEGMENT_OFFSET(w1));
                if (it != imageOffsetToTex.end()) {
                    w1 = 0xFF000000u | (it->second & 0x00FFFFFFu);
                }
            }
            writer.Write(w0);
            writer.Write(w1);
        }
    }

    // ── Texture metadata ──────────────────────────────────────────────────────
    for (const auto& tex : model->mTexInfos) {
        writer.Write(tex.type);
        writer.Write(tex.width);
        writer.Write(tex.height);
        writer.Write(tex.tlutColors);
        writer.Write(tex.textureDataOffset);
    }

    // ── Raw texture data blob ────────────────────────────────────────────────
    // [port] The whole contiguous texture data area from the decompressed model. Keeps the
    // animated texture frames, plus any bytes wedged between listed textures that DL commands
    // reach via segment offsets.
    writer.Write(model->mTexDataSize);
    if (model->mTexDataSize > 0 && !model->mRawTexData.empty()) {
        writer.Write((char*)model->mRawTexData.data(), model->mRawTexData.size());
    }

    // ── Animation list ────────────────────────────────────────────────────────
    if (model->mHasAnimation) {
        writer.Write(model->mAnimHeader.scalingFactor);
        writer.Write(static_cast<uint16_t>(model->mBones.size()));
        for (const auto& bone : model->mBones) {
            writer.Write(bone.pos[0]);
            writer.Write(bone.pos[1]);
            writer.Write(bone.pos[2]);
            writer.Write(bone.id);
            writer.Write(bone.parentId);
        }
    }

    // ── Collision list ────────────────────────────────────────────────────────
    if (model->mHasCollision) {
        const auto& col = model->mCollisionHeader;
        writer.Write(col.minIndex[0]);
        writer.Write(col.minIndex[1]);
        writer.Write(col.minIndex[2]);
        writer.Write(col.maxIndex[0]);
        writer.Write(col.maxIndex[1]);
        writer.Write(col.maxIndex[2]);
        writer.Write(col.yStride);
        writer.Write(col.zStride);
        writer.Write(col.geoCubeScale);
        writer.Write(static_cast<uint16_t>(model->mGeoCubes.size()));
        writer.Write(static_cast<uint16_t>(model->mCollisionTris.size()));
        for (const auto& cube : model->mGeoCubes) {
            writer.Write(cube.startTri);
            writer.Write(cube.triCount);
        }
        for (const auto& tri : model->mCollisionTris) {
            writer.Write(tri.vtxIds[0]);
            writer.Write(tri.vtxIds[1]);
            writer.Write(tri.vtxIds[2]);
            writer.Write(tri.unk6);
            writer.Write(tri.flags);
        }
    }

    // ── Unk14 (hitbox) ────────────────────────────────────────────────────────
    if (model->mHasUnk14) {
        writer.Write(static_cast<int16_t>(model->mUnk14Entries0.size()));
        writer.Write(static_cast<int16_t>(model->mUnk14Entries1.size()));
        writer.Write(static_cast<int16_t>(model->mUnk14Entries2.size()));
        writer.Write(model->mUnk14Unk6);
        for (const auto& e : model->mUnk14Entries0) {
            writer.Write(e.scale1[0]);
            writer.Write(e.scale1[1]);
            writer.Write(e.scale1[2]);
            writer.Write(e.scale2[0]);
            writer.Write(e.scale2[1]);
            writer.Write(e.scale2[2]);
            writer.Write(e.pos[0]);
            writer.Write(e.pos[1]);
            writer.Write(e.pos[2]);
            writer.Write(e.rot[0]);
            writer.Write(e.rot[1]);
            writer.Write(e.rot[2]);
            writer.Write(e.unk15);
            writer.Write(e.animIndex);
            writer.Write(e.pad);
        }
        for (const auto& e : model->mUnk14Entries1) {
            writer.Write(e.unk0);
            writer.Write(e.unk2);
            writer.Write(e.pos[0]);
            writer.Write(e.pos[1]);
            writer.Write(e.pos[2]);
            writer.Write(e.rot[0]);
            writer.Write(e.rot[1]);
            writer.Write(e.rot[2]);
            writer.Write(e.unkD);
            writer.Write(e.animIndex);
            writer.Write(e.pad);
        }
        for (const auto& e : model->mUnk14Entries2) {
            writer.Write(e.unk0);
            writer.Write(e.unk2[0]);
            writer.Write(e.unk2[1]);
            writer.Write(e.unk2[2]);
            writer.Write(e.unk8);
            writer.Write(e.unk9);
            writer.Write(e.pad[0]);
            writer.Write(e.pad[1]);
        }
    }

    // ── Unk20 ─────────────────────────────────────────────────────────────────
    if (model->mHasUnk20) {
        writer.Write(static_cast<uint8_t>(model->mUnk20Entries.size()));
        for (const auto& e : model->mUnk20Entries) {
            writer.Write(e.unk0[0]);
            writer.Write(e.unk0[1]);
            writer.Write(e.unk0[2]);
            writer.Write(e.unk6[0]);
            writer.Write(e.unk6[1]);
            writer.Write(e.unk6[2]);
            writer.Write(e.unkC);
            writer.Write(e.pad);
        }
    }

    // ── Effects ───────────────────────────────────────────────────────────────
    if (!model->mEffects.empty()) {
        writer.Write(static_cast<uint16_t>(model->mEffects.size()));
        for (const auto& fx : model->mEffects) {
            writer.Write(fx.dataInfo);
            writer.Write(static_cast<uint16_t>(fx.vtxIndices.size()));
            for (auto idx : fx.vtxIndices) {
                writer.Write(idx);
            }
        }
    }

    // ── Unk28 ─────────────────────────────────────────────────────────────────
    if (model->mHasUnk28) {
        writer.Write(static_cast<int16_t>(model->mUnk28Entries.size()));
        for (const auto& e : model->mUnk28Entries) {
            writer.Write(e.coord[0]);
            writer.Write(e.coord[1]);
            writer.Write(e.coord[2]);
            writer.Write(e.animIndex);
            writer.Write(static_cast<int8_t>(e.vtxList.size()));
            for (auto idx : e.vtxList) {
                writer.Write(idx);
            }
        }
    }

    // ── Animated textures (always 4 slots) ───────────────────────────────────
    if (!model->mAnimTextures.empty()) {
        for (const auto& at : model->mAnimTextures) {
            writer.Write(at.frameSize);
            writer.Write(at.frameCount);
            writer.Write(at.frameRate);
        }
    }

    writer.Finish(write);

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> ModelFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto modelOffset = GetSafeNode<uint32_t>(node, "offset"); // Should always be 0 in reality
    const auto modelOffsetEnd = modelOffset + segment.size;
    const auto fileOffset = Companion::Instance->GetCurrentVRAM().value().offset;

    if (reader.ReadInt32() != BK64_MODEL_HEADER) {
        SPDLOG_ERROR("Invalid Header For BK64 Model {}", symbol);
        return std::nullopt;
    }

    /* 0x04 */ auto geoLayoutOffset = reader.ReadUInt32();
    /* 0x08 */ auto textureSetupOffset = reader.ReadUInt16();
    /* 0x0A */ auto geoType = reader.ReadUInt16();
    /* 0x0C */ auto displayListSetupOffset = reader.ReadUInt32();
    /* 0x10 */ auto vertexSetupOffset = reader.ReadUInt32();
    /* 0x14 */ auto unkHitboxInfoOffset = reader.ReadUInt32();
    /* 0x18 */ auto animationSetupOffset = reader.ReadUInt32();
    /* 0x1C */ auto collisionSetupOffset = reader.ReadUInt32();
    /* 0x20 */ auto modelUnk20Offset = reader.ReadUInt32();
    /* 0x24 */ auto effectsSetupOffset = reader.ReadUInt32();
    /* 0x28 */ auto modelUnk28Offset = reader.ReadUInt32();
    /* 0x2C */ auto animatedTextureOffset = reader.ReadUInt32();
    /* 0x30 */ auto triCount = reader.ReadUInt16();
    /* 0x32 */ auto vertCount = reader.ReadUInt16();

    auto modelData = std::make_shared<ModelData>(geoType, triCount, vertCount);

    uint16_t textureCount;

    if (geoLayoutOffset != 0) {
        SPDLOG_INFO("HAS GL {}", symbol);
        modelData->mHasGeo = true;
        YAML::Node geoLayout;
        geoLayout["type"] = "BK64:GEO_LAYOUT";
        geoLayout["offset"] = modelOffset + geoLayoutOffset;
        geoLayout["symbol"] = symbol + "_GEO";
        Companion::Instance->AddAsset(geoLayout);
    }

    if (textureSetupOffset != 0) {

        reader.Seek(modelOffset + textureSetupOffset, LUS::SeekOffsetType::Start);

        auto textureDataSize = reader.ReadUInt32();
        textureCount = reader.ReadUInt16();
        reader.ReadUInt16(); // pad

        Companion::Instance->SetCompressedSegment(2, fileOffset,
                                                  modelOffset + textureSetupOffset + TEXTURE_HEADER_SIZE +
                                                      textureCount * TEXTURE_METADATA_SIZE);

        for (uint16_t i = 0; i < textureCount; i++) {
            auto textureDataOffset = reader.ReadUInt32();
            auto textureType = reader.ReadUInt16();
            reader.ReadUInt16(); // pad
            uint32_t width = reader.ReadUByte();
            uint32_t height = reader.ReadUByte();
            reader.ReadUInt16(); // pad
            reader.ReadUInt32(); // pad

            std::string format;
            std::string ctype;
            uint32_t tlutSize = 0;
            uint16_t tlutColors = 0;

            // Stash texture metadata for the binary exporter. Type 0x1 just means "has a TLUT" —
            // it's both CI4 and CI8. We can't tell which until all the headers are in, so the real
            // bit depth gets resolved further down.
            TexInfo texInfo;
            texInfo.type = textureType;
            texInfo.width = static_cast<uint8_t>(width);
            texInfo.height = static_cast<uint8_t>(height);
            texInfo.tlutColors = 0;
            texInfo.textureDataOffset = textureDataOffset;

            switch (textureType) {
                case 0x1:
                    // Sorted out later, once every header is read
                    break;
                case 0x2:
                    texInfo.tlutColors = 0x100;
                    break;
                case 0x4:
                case 0x8:
                case 0x10:
                    break;
                default:
                    throw std::runtime_error("BK64::ModelFactory: Invalid Texture Format Found " +
                                             std::to_string(textureType));
            }

            modelData->mTexInfos.push_back(texInfo);
        }

        uint32_t texDataStart =
            modelOffset + textureSetupOffset + TEXTURE_HEADER_SIZE + textureCount * TEXTURE_METADATA_SIZE;
        modelData->mTexDataSize = textureDataSize;

        // Now disambiguate the type 0x1 textures. 0x1 means "has TLUT", which is either CI4
        // (16-entry palette) or CI8 (256-entry palette) — the header doesn't say which. Trick is
        // to measure the gap to the next texture: if it's big enough for a full CI8 payload
        // (0x200 TLUT + W*H pixels), call it CI8, otherwise CI4. The last texture in a list can be
        // padded, hence >= instead of ==. CI8 always needs more room than CI4 at the same W*H
        // (delta = 0x1E0 - W*H/2 > 0 for any BK texture up to 64x64), so there's no overlap to
        // worry about.
        for (uint16_t i = 0; i < textureCount; i++) {
            auto& tex = modelData->mTexInfos[i];
            if (tex.type != 0x1) {
                continue;
            }

            uint32_t nextOffset =
                (i + 1 < textureCount) ? modelData->mTexInfos[i + 1].textureDataOffset : textureDataSize;
            uint32_t gap = nextOffset - tex.textureDataOffset;
            uint32_t ci4Size = 0x20 + ((uint32_t)tex.width * tex.height) / 2; // 16-entry TLUT + CI4 pixels
            uint32_t ci8Size = 0x200 + (uint32_t)tex.width * tex.height;      // 256-entry TLUT + CI8 pixels

            if (gap >= ci8Size) {
                tex.type = 0x2; // CI8
                tex.tlutColors = 0x100;
                if (gap != ci8Size) {
                    SPDLOG_INFO("[BK64::Model] tex[{}] {}x{}: gap=0x{:X} >= CI8 (0x{:X}), classified CI8 (pad=0x{:X})",
                                i, tex.width, tex.height, gap, ci8Size, gap - ci8Size);
                }
            } else {
                tex.tlutColors = 0x10; // CI4
                if (gap < ci4Size) {
                    SPDLOG_WARN("[BK64::Model] tex[{}] {}x{}: gap=0x{:X} smaller than CI4 (0x{:X}), data may be "
                                "truncated",
                                i, tex.width, tex.height, gap, ci4Size);
                } else if (gap != ci4Size) {
                    SPDLOG_INFO("[BK64::Model] tex[{}] {}x{}: gap=0x{:X} (CI4 0x{:X}, pad=0x{:X})", i, tex.width,
                                tex.height, gap, ci4Size, gap - ci4Size);
                }
            }
        }

        // [port] Grab the entire raw texture area so animated frames and any unlisted bytes
        // between textures survive into the binary.
        if (textureDataSize > 0 && texDataStart + textureDataSize <= segment.size) {
            modelData->mRawTexData.assign(segment.data + texDataStart, segment.data + texDataStart + textureDataSize);
        }

        // [port] Also emit each texture as its own OTEX resource for modders. The raw blob above
        // already keeps the animated frames intact; these per-texture resources are the hook for
        // dropping in replacement textures that the importer overlays on top.
        for (uint16_t i = 0; i < textureCount; i++) {
            const auto& tex = modelData->mTexInfos[i];
            uint32_t texOffset = texDataStart + tex.textureDataOffset;

            std::string format;
            uint32_t tlutByteSize = 0;

            switch (tex.type) {
                case 0x1:
                    format = "CI4";
                    tlutByteSize = tex.tlutColors * 2;
                    break;
                case 0x2:
                    format = "CI8";
                    tlutByteSize = tex.tlutColors * 2;
                    break;
                case 0x4:
                    format = "RGBA16";
                    break;
                case 0x8:
                    format = "RGBA32";
                    break;
                case 0x10:
                    format = "IA8";
                    break;
                default:
                    continue;
            }

            std::string texSymbol = symbol + "_tex_" + std::to_string(i);

            if (tlutByteSize > 0) {
                YAML::Node tlut;
                tlut["type"] = "TEXTURE";
                tlut["offset"] = texOffset;
                tlut["format"] = "TLUT";
                tlut["ctype"] = "u16";
                tlut["colors"] = (int)tex.tlutColors;
                tlut["symbol"] = texSymbol + "_TLUT";
                Companion::Instance->AddAsset(tlut);
            }

            YAML::Node texture;
            texture["type"] = "TEXTURE";
            texture["offset"] = texOffset + tlutByteSize;
            texture["format"] = format;
            texture["ctype"] = "u16";
            texture["width"] = (int)tex.width;
            texture["height"] = (int)tex.height;
            texture["symbol"] = texSymbol;
            if (tlutByteSize > 0) {
                texture["tlut_symbol"] = texSymbol + "_TLUT";
            }
            Companion::Instance->AddAsset(texture);
        }
    }

    // Parse First To Avoid Auto Extraction By DLs
    if (vertexSetupOffset != 0) {
        reader.Seek(modelOffset + vertexSetupOffset, LUS::SeekOffsetType::Start);
        Companion::Instance->SetCompressedSegment(1, fileOffset, modelOffset + vertexSetupOffset + VTX_HEADER_SIZE);

        modelData->mHasVtx = true;
        modelData->mVtxHeader.minCoord[0] = reader.ReadInt16();
        modelData->mVtxHeader.minCoord[1] = reader.ReadInt16();
        modelData->mVtxHeader.minCoord[2] = reader.ReadInt16();
        modelData->mVtxHeader.maxCoord[0] = reader.ReadInt16();
        modelData->mVtxHeader.maxCoord[1] = reader.ReadInt16();
        modelData->mVtxHeader.maxCoord[2] = reader.ReadInt16();
        modelData->mVtxHeader.centerCoord[0] = reader.ReadInt16();
        modelData->mVtxHeader.centerCoord[1] = reader.ReadInt16();
        modelData->mVtxHeader.centerCoord[2] = reader.ReadInt16();
        modelData->mVtxHeader.localNorm = reader.ReadInt16();
        modelData->mVtxHeader.count = reader.ReadUInt16();
        modelData->mVtxHeader.globalNorm = reader.ReadInt16();

        // The header vtx count lies for some models, so derive the real count from the byte span
        // between the VTX section and whatever section comes next.
        constexpr uint32_t kVtxRawSize = 16; // sizeof(Vtx) in the ROM
        const uint32_t vtxDataStart = vertexSetupOffset + VTX_HEADER_SIZE;
        uint32_t vtxDataEnd = static_cast<uint32_t>(modelOffsetEnd - modelOffset);
        for (uint32_t candidate : { geoLayoutOffset, static_cast<uint32_t>(textureSetupOffset), displayListSetupOffset,
                                    unkHitboxInfoOffset, animationSetupOffset, collisionSetupOffset, modelUnk20Offset,
                                    effectsSetupOffset, modelUnk28Offset, animatedTextureOffset }) {
            if (candidate > vtxDataStart && candidate < vtxDataEnd) {
                vtxDataEnd = candidate;
            }
        }
        const uint32_t trueVtxCount = (vtxDataEnd - vtxDataStart) / kVtxRawSize;
        if (trueVtxCount != static_cast<uint32_t>(modelData->mVtxHeader.count)) {
            SPDLOG_DEBUG("[BKModel] {} vtx header count {} vs section-derived "
                         "count {} — using section-derived",
                         symbol, modelData->mVtxHeader.count, trueVtxCount);
            modelData->mVtxHeader.count = static_cast<uint16_t>(trueVtxCount);
        }

        // We hold off registering _VTX until after the DL bytes are read (so the count can grow
        // to cover DL refs), but it has to land BEFORE AddAsset(gfxNode) kicks off sub-DL parsing.
        // Register it too late and the DL G_VTX scans run with no _VTX in the registry — SearchVtx
        // then misses every reference into the model's own vtx region and spits out a flat autogen
        // entry per reference.
    }

    // Read the DL bytes and work out the sub-DL boundaries — but don't register the GFX assets
    // yet. Registering triggers DL parsing, and the DL parser wants _VTX in the registry first
    // (see above).
    std::set<uint32_t> dlOffsets;
    if (displayListSetupOffset != 0) {
        reader.Seek(modelOffset + displayListSetupOffset, LUS::SeekOffsetType::Start);
        Companion::Instance->SetCompressedSegment(3, fileOffset,
                                                  modelOffset + displayListSetupOffset + GFX_HEADER_SIZE);
        modelData->mHasDL = true;
        auto dlCount = reader.ReadUInt32();
        auto unkDLInfo = reader.ReadUInt32();
        modelData->mDLCount = dlCount;
        modelData->mDLUnkInfo = unkDLInfo;

        uint32_t dlOffset = 0;
        if (dlCount > 0) {
            dlOffsets.emplace(dlOffset);
        }
        modelData->mRawDLWords.reserve(dlCount * 2);
        while (dlOffset < dlCount * GFX_CMD_SIZE) {
            auto w0 = reader.ReadUInt32();
            auto w1 = reader.ReadUInt32();
            modelData->mRawDLWords.push_back(w0);
            modelData->mRawDLWords.push_back(w1);
            dlOffset += GFX_CMD_SIZE;
            uint8_t opCode = w0 >> 24;

            if (opCode == GBI(G_ENDDL) && dlOffset != dlCount * GFX_CMD_SIZE) {
                dlOffsets.emplace(dlOffset);
            }
            // G_DL jump targets inside segment 3 are split points too. Splitting on G_ENDDL only
            // catches the sequential sub-lists; an intra-buffer G_DL can jump to some arbitrary
            // offset that no G_ENDDL precedes.
            if (opCode == GBI(G_DL) && SEGMENT_NUMBER(w1) == 3) {
                dlOffsets.emplace(SEGMENT_OFFSET(w1));
            }
        }
    }

    // That section-boundary heuristic can still undercount, so scan the DL for the highest vertex
    // index it actually touches. That's the count we trust.
    if (modelData->mHasVtx && modelData->mHasDL && !modelData->mRawDLWords.empty()) {
        constexpr uint32_t kN64VtxSize = 16;
        uint32_t maxVtxNeeded = modelData->mVtxHeader.count;
        for (size_t i = 0; i < modelData->mRawDLWords.size(); i += 2) {
            uint32_t w0 = modelData->mRawDLWords[i];
            uint32_t w1 = modelData->mRawDLWords[i + 1];
            uint8_t opCode = w0 >> 24;
            if (opCode == GBI(G_VTX) && SEGMENT_NUMBER(w1) == 1) {
                uint32_t n = (w0 >> 10) & 0x3F;
                uint32_t off = SEGMENT_OFFSET(w1);
                uint32_t vtxEnd = off / kN64VtxSize + n;
                if (vtxEnd > maxVtxNeeded) {
                    maxVtxNeeded = vtxEnd;
                }
            }
        }
        if (maxVtxNeeded > modelData->mVtxHeader.count) {
            SPDLOG_WARN("[BKModel] {} DL references vertex {} but header count is {} — extending to {}", symbol,
                        maxVtxNeeded - 1, modelData->mVtxHeader.count, maxVtxNeeded);
            modelData->mVtxHeader.count = static_cast<uint16_t>(maxVtxNeeded);
        }
    }

    // Register _VTX with the corrected count now, before the GFX sub-DLs go in — the sub-DL
    // G_VTX handler leans on SearchVtx finding this entry, otherwise it autogens per reference.
    if (modelData->mHasVtx) {
        YAML::Node vtx;
        vtx["type"] = "VTX";
        vtx["count"] = modelData->mVtxHeader.count;
        vtx["offset"] = modelOffset + vertexSetupOffset + VTX_HEADER_SIZE;
        vtx["symbol"] = symbol + "_VTX";
        Companion::Instance->AddAsset(vtx);
    }

    // Safe to register the sub-DLs now; their parse pass resolves segmented vtx refs against the
    // _VTX we just registered.
    if (displayListSetupOffset != 0) {
        uint32_t count = 0;
        for (const auto& extractOffset : dlOffsets) {
            YAML::Node gfxNode;
            gfxNode["type"] = "GFX";
            gfxNode["offset"] = modelOffset + displayListSetupOffset + GFX_HEADER_SIZE + extractOffset;
            gfxNode["symbol"] = symbol + "_GFX_" + std::to_string(count);
            // Binary export only: we parse these purely for the side effect of auto-registering
            // VTX sub-assets, but skip writing a per-sub-DL entry. The raw DL words already live
            // in the parent model resource, and emitting each one separately can shove us past the
            // 65,535-entry ZIP limit. Code and Header exports still write the standalone entries.
            if (Companion::Instance->GetConfig().exporterType == ExportType::Binary) {
                gfxNode["no_export"] = true;
            }
            Companion::Instance->AddAsset(gfxNode);
            count++;
        }
        modelData->mGfxSubListCount = count;
    }

    if (unkHitboxInfoOffset != 0) {
        reader.Seek(modelOffset + unkHitboxInfoOffset, LUS::SeekOffsetType::Start);
        modelData->mHasUnk14 = true;
        auto count1 = reader.ReadInt16();
        auto count2 = reader.ReadInt16();
        auto count3 = reader.ReadInt16();
        modelData->mUnk14Unk6 = reader.ReadInt16();

        for (int16_t i = 0; i < count1; i++) {
            Unk14_0 e{};
            e.scale1[0] = reader.ReadInt16();
            e.scale1[1] = reader.ReadInt16();
            e.scale1[2] = reader.ReadInt16();
            e.scale2[0] = reader.ReadInt16();
            e.scale2[1] = reader.ReadInt16();
            e.scale2[2] = reader.ReadInt16();
            e.pos[0] = reader.ReadInt16();
            e.pos[1] = reader.ReadInt16();
            e.pos[2] = reader.ReadInt16();
            e.rot[0] = reader.ReadUByte();
            e.rot[1] = reader.ReadUByte();
            e.rot[2] = reader.ReadUByte();
            e.unk15 = reader.ReadUByte();
            e.animIndex = reader.ReadUByte();
            e.pad = reader.ReadUByte();
            modelData->mUnk14Entries0.push_back(e);
        }

        for (int16_t i = 0; i < count2; i++) {
            Unk14_1 e{};
            e.unk0 = reader.ReadInt16();
            e.unk2 = reader.ReadInt16();
            e.pos[0] = reader.ReadInt16();
            e.pos[1] = reader.ReadInt16();
            e.pos[2] = reader.ReadInt16();
            e.rot[0] = reader.ReadUByte();
            e.rot[1] = reader.ReadUByte();
            e.rot[2] = reader.ReadUByte();
            e.unkD = reader.ReadUByte();
            e.animIndex = reader.ReadUByte();
            e.pad = reader.ReadUByte();
            modelData->mUnk14Entries1.push_back(e);
        }

        for (int16_t i = 0; i < count3; i++) {
            Unk14_2 e{};
            e.unk0 = reader.ReadInt16();
            e.unk2[0] = reader.ReadInt16();
            e.unk2[1] = reader.ReadInt16();
            e.unk2[2] = reader.ReadInt16();
            e.unk8 = reader.ReadUByte();
            e.unk9 = reader.ReadUByte();
            e.pad[0] = reader.ReadUByte();
            e.pad[1] = reader.ReadUByte();
            modelData->mUnk14Entries2.push_back(e);
        }
    }

    if (animationSetupOffset != 0) {
        reader.Seek(modelOffset + animationSetupOffset, LUS::SeekOffsetType::Start);
        modelData->mHasAnimation = true;
        modelData->mAnimHeader.scalingFactor = reader.ReadFloat();
        auto boneCount = reader.ReadUInt16();
        reader.ReadUInt16(); // pad

        for (uint16_t i = 0; i < boneCount; i++) {
            BoneData bone;
            bone.pos[0] = reader.ReadFloat();
            bone.pos[1] = reader.ReadFloat();
            bone.pos[2] = reader.ReadFloat();
            bone.id = reader.ReadUInt16();
            bone.parentId = reader.ReadUInt16();
            modelData->mBones.push_back(bone);
        }
    }

    if (collisionSetupOffset != 0) {
        constexpr size_t kCollHeaderSize = 0x18;
        constexpr size_t kGeoCubeSize = 4;
        constexpr size_t kCollTriSize = 12;
        auto looksLikeCollisionList = [&](uint32_t at) -> bool {
            if (at + kCollHeaderSize > segment.size) {
                return false;
            }
            uint16_t geoCnt = (uint16_t)((segment.data[at + 0x10] << 8) | segment.data[at + 0x11]);
            uint16_t triCnt = (uint16_t)((segment.data[at + 0x14] << 8) | segment.data[at + 0x15]);
            size_t needed =
                (size_t)kCollHeaderSize + (size_t)geoCnt * kGeoCubeSize + (size_t)triCnt * kCollTriSize;
            return at + needed <= segment.size;
        };

        uint32_t collAt = modelOffset + collisionSetupOffset;
        bool collOk = looksLikeCollisionList(collAt);
        if (!collOk) {
            for (int delta : { 1, -1, 2, -2, 3, -3, 4, -4 }) {
                int64_t candidate = (int64_t)collAt + delta;
                if (candidate < (int64_t)modelOffset) {
                    continue;
                }
                if (looksLikeCollisionList((uint32_t)candidate)) {
                    SPDLOG_WARN("[BKModel] {} collisionSetupOffset 0x{:X} fails structural check — recovered "
                                "real section at 0x{:X} (delta {:+d})",
                                symbol, collisionSetupOffset, (uint32_t)candidate - modelOffset, delta);
                    collAt = (uint32_t)candidate;
                    collOk = true;
                    break;
                }
            }
        }
        if (!collOk) {
            SPDLOG_ERROR("[BKModel] {} collisionSetupOffset 0x{:X} fails structural check and no nearby valid "
                         "BKCollisionList found; skipping collision section (segSize 0x{:X})",
                         symbol, collisionSetupOffset, segment.size);
        } else {
            reader.Seek(collAt, LUS::SeekOffsetType::Start);

            modelData->mHasCollision = true;
            modelData->mCollisionHeader.minIndex[0] = reader.ReadInt16();
            modelData->mCollisionHeader.minIndex[1] = reader.ReadInt16();
            modelData->mCollisionHeader.minIndex[2] = reader.ReadInt16();
            modelData->mCollisionHeader.maxIndex[0] = reader.ReadInt16();
            modelData->mCollisionHeader.maxIndex[1] = reader.ReadInt16();
            modelData->mCollisionHeader.maxIndex[2] = reader.ReadInt16();
            modelData->mCollisionHeader.yStride = reader.ReadUInt16();
            modelData->mCollisionHeader.zStride = reader.ReadUInt16();
            auto geoCubeCount = reader.ReadUInt16();
            modelData->mCollisionHeader.geoCubeScale = reader.ReadUInt16();
            auto triCount = reader.ReadUInt16();
            reader.ReadUInt16(); // pad

            for (uint16_t i = 0; i < geoCubeCount; i++) {
                GeoCube cube;
                cube.startTri = reader.ReadUInt16();
                cube.triCount = reader.ReadUInt16();
                modelData->mGeoCubes.push_back(cube);
            }

            for (uint16_t i = 0; i < triCount; i++) {
                CollisionTri tri;
                tri.vtxIds[0] = reader.ReadUInt16();
                tri.vtxIds[1] = reader.ReadUInt16();
                tri.vtxIds[2] = reader.ReadUInt16();
                tri.unk6 = reader.ReadUInt16();
                tri.flags = reader.ReadUInt32();
                modelData->mCollisionTris.push_back(tri);
            }
        }
    }

    if (modelUnk20Offset != 0) {
        reader.Seek(modelOffset + modelUnk20Offset, LUS::SeekOffsetType::Start);
        modelData->mHasUnk20 = true;
        auto count = reader.ReadInt8();
        reader.ReadInt8(); // pad

        for (int8_t i = 0; i < count; i++) {
            Unk20_0 e{};
            e.unk0[0] = reader.ReadInt16();
            e.unk0[1] = reader.ReadInt16();
            e.unk0[2] = reader.ReadInt16();
            e.unk6[0] = reader.ReadInt16();
            e.unk6[1] = reader.ReadInt16();
            e.unk6[2] = reader.ReadInt16();
            e.unkC = reader.ReadUByte();
            e.pad = reader.ReadUByte();
            modelData->mUnk20Entries.push_back(e);
        }
    }

    if (effectsSetupOffset != 0) {
        reader.Seek(modelOffset + effectsSetupOffset, LUS::SeekOffsetType::Start);
        auto effectCount = reader.ReadUInt16();

        for (uint16_t i = 0; i < effectCount; i++) {
            Effect effect;
            effect.dataInfo = reader.ReadUInt16();
            auto vtxCount = reader.ReadUInt16();
            for (uint16_t j = 0; j < vtxCount; j++) {
                effect.vtxIndices.push_back(reader.ReadUInt16());
            }
            modelData->mEffects.push_back(effect);
        }
    }

    if (modelUnk28Offset != 0) {
        SPDLOG_INFO("HAS UNK 28");
        reader.Seek(modelOffset + modelUnk28Offset, LUS::SeekOffsetType::Start);
        modelData->mHasUnk28 = true;
        auto count = reader.ReadInt16();
        reader.ReadInt16(); // pad

        for (int16_t i = 0; i < count; i++) {
            Unk28_0 e{};
            e.coord[0] = reader.ReadInt16();
            e.coord[1] = reader.ReadInt16();
            e.coord[2] = reader.ReadInt16();
            e.animIndex = reader.ReadInt8();
            auto vtxCount = reader.ReadInt8();
            for (int16_t j = 0; j < vtxCount; j++) {
                e.vtxList.push_back(reader.ReadInt16());
            }
            modelData->mUnk28Entries.push_back(e);
        }
    }

    if (animatedTextureOffset != 0) {
        reader.Seek(modelOffset + animatedTextureOffset, LUS::SeekOffsetType::Start);
        for (uint32_t i = 0; i < ANIM_TEXTURE_LIST_COUNT; i++) {
            AnimTexture animTexture;
            animTexture.frameSize = reader.ReadUInt16();
            animTexture.frameCount = reader.ReadUInt16();
            animTexture.frameRate = reader.ReadFloat();

            // Point the segment at frame 0's texture
            if (animTexture.frameSize != 0) {
                Companion::Instance->SetCompressedSegment(15 - i, fileOffset,
                                                          modelOffset + textureSetupOffset + TEXTURE_HEADER_SIZE +
                                                              textureCount * TEXTURE_METADATA_SIZE);
            }

            modelData->mAnimTextures.push_back(animTexture);
        }
    }

    return modelData;
}

} // namespace BK64
