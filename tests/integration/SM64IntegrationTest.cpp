#include <gtest/gtest.h>
#include "IntegrationTestHelpers.h"
#include "factories/ResourceType.h"

static const std::string SM64_US_ROM = "sm64.us.z64";
static const std::string SM64_US_CONFIG_DIR = GetTestDir() + "/sm64/us";

class SM64USIntegrationTest : public ::testing::Test {
protected:
    static std::map<std::string, std::vector<char>> sAssets;
    static bool sPipelineRan;

    static void SetUpTestSuite() {
        if (!RomExists(SM64_US_ROM)) {
            return;
        }
        auto romPath = GetRomDir() + "/" + SM64_US_ROM;
        sAssets = RunPipeline(SM64_US_CONFIG_DIR, romPath);
        sPipelineRan = true;
    }

    void SetUp() override {
        if (!RomExists(SM64_US_ROM)) {
            GTEST_SKIP() << "SM64 US ROM not found at " << GetRomDir() << "/" << SM64_US_ROM;
        }
        ASSERT_TRUE(sPipelineRan) << "Pipeline did not run successfully";
        if (sAssets.empty()) {
            GTEST_SKIP() << "Pipeline produced no assets (missing config.yml?)";
        }
    }

    const std::vector<char>& GetAsset(const std::string& name) {
        const std::string suffix = "/" + name;
        for (auto& [key, value] : sAssets) {
            if (key == name || key.ends_with(suffix)) {
                return value;
            }
        }
        static std::vector<char> empty;
        return empty;
    }
};

std::map<std::string, std::vector<char>> SM64USIntegrationTest::sAssets;
bool SM64USIntegrationTest::sPipelineRan = false;

TEST_F(SM64USIntegrationTest, PipelineProducesAssets) {
    EXPECT_FALSE(sAssets.empty()) << "Pipeline produced no assets";
}

TEST_F(SM64USIntegrationTest, TextureRGBA16) {
    auto& data = GetAsset("test_texture");
    ASSERT_FALSE(data.empty()) << "Texture asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Texture));

    // After 0x40 header: uint32 format, uint32 width, uint32 height, uint32 buf_size, then buffer
    ASSERT_GE(data.size(), 0x50u) << "Texture too small to contain metadata";

    uint32_t width, height, bufSize;
    std::memcpy(&width, data.data() + 0x44, 4);
    std::memcpy(&height, data.data() + 0x48, 4);
    std::memcpy(&bufSize, data.data() + 0x4C, 4);

    EXPECT_EQ(width, 32u);
    EXPECT_EQ(height, 32u);
    EXPECT_EQ(bufSize, 2048u);
    EXPECT_EQ(data.size(), 0x40u + 16u + 2048u) << "Total size mismatch";
}

TEST_F(SM64USIntegrationTest, VtxBasic) {
    auto& data = GetAsset("test_vtx");
    ASSERT_FALSE(data.empty()) << "VTX asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Vertex));

    // After 0x40 header: uint32 count, then count * 16 bytes per vertex
    ASSERT_GE(data.size(), 0x44u) << "VTX too small to contain count";

    uint32_t count;
    std::memcpy(&count, data.data() + 0x40, 4);

    EXPECT_EQ(count, 4u);
    EXPECT_EQ(data.size(), 0x40u + 4u + count * 16u) << "Total size mismatch";
}

TEST_F(SM64USIntegrationTest, BlobBasic) {
    auto& data = GetAsset("test_blob");
    ASSERT_FALSE(data.empty()) << "Blob asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Blob));

    // After 0x40 header: uint32 size, then raw data
    ASSERT_GE(data.size(), 0x44u) << "Blob too small to contain size";

    uint32_t blobSize;
    std::memcpy(&blobSize, data.data() + 0x40, 4);

    EXPECT_EQ(blobSize, 64u);
    EXPECT_EQ(data.size(), 0x40u + 4u + blobSize) << "Total size mismatch";
}

TEST_F(SM64USIntegrationTest, CollisionBasic) {
    auto& data = GetAsset("test_collision");
    ASSERT_FALSE(data.empty()) << "Collision asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Collision));

    // After 0x40 header: uint32 command_count, then command_count * int16_t
    ASSERT_GE(data.size(), 0x44u) << "Collision too small to contain count";

    uint32_t cmdCount;
    std::memcpy(&cmdCount, data.data() + 0x40, 4);

    EXPECT_GT(cmdCount, 0u) << "Expected at least one collision command";
    EXPECT_EQ(data.size(), 0x40u + 4u + cmdCount * 2u) << "Total size mismatch";
}

TEST_F(SM64USIntegrationTest, GfxBasic) {
    auto& data = GetAsset("test_gfx");
    ASSERT_FALSE(data.empty()) << "GFX asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::DisplayList));

    // After 0x40 header: int8 GBI version, padding to 8-byte align, then marker + commands
    ASSERT_GE(data.size(), 0x50u) << "GFX too small";

    // GBI version byte at 0x40
    int8_t gbiVersion;
    std::memcpy(&gbiVersion, data.data() + 0x40, 1);
    EXPECT_GE(gbiVersion, 0) << "Invalid GBI version";

    // BEEFBEEF marker at 0x4C (after 8-byte aligned padding + G_MARKER word)
    uint32_t beefMarker;
    std::memcpy(&beefMarker, data.data() + 0x4C, 4);
    EXPECT_EQ(beefMarker, 0xBEEFBEEFu) << "Missing BEEFBEEF marker";
}

TEST_F(SM64USIntegrationTest, LightsBasic) {
    auto& data = GetAsset("test_lights");
    ASSERT_FALSE(data.empty()) << "Lights asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Lights));

    // After 0x40 header: raw Lights1Raw struct (24 bytes)
    EXPECT_EQ(data.size(), 0x40u + 24u) << "Lights size mismatch";
}

TEST_F(SM64USIntegrationTest, AnimBasic) {
    auto& data = GetAsset("test_anim");
    ASSERT_FALSE(data.empty()) << "Anim asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Anim));

    // After 0x40 header: 6x int16 (12 bytes) + uint64 length (8 bytes)
    // + uint32 indices count + indices + uint32 entries count + entries
    ASSERT_GE(data.size(), 0x40u + 20u + 4u) << "Anim too small";

    // Read indices count at offset 0x54 (0x40 + 12 + 8)
    uint32_t indicesCount;
    std::memcpy(&indicesCount, data.data() + 0x54, 4);
    EXPECT_GT(indicesCount, 0u) << "Expected at least one animation index";
}

TEST_F(SM64USIntegrationTest, DialogBasic) {
    auto& data = GetAsset("test_dialog");
    ASSERT_FALSE(data.empty()) << "Dialog asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::SDialog));

    // After 0x40 header: uint32 unused + int8 linesPerBox + int16 leftOffset + int16 width
    // + uint32 text size + text data
    ASSERT_GE(data.size(), 0x40u + 4u + 1u + 2u + 2u + 4u) << "Dialog too small";

    // Read text size at offset 0x49 (0x40 + 4 + 1 + 2 + 2)
    uint32_t textSize;
    std::memcpy(&textSize, data.data() + 0x49, 4);
    EXPECT_GT(textSize, 0u) << "Expected non-empty dialog text";
    EXPECT_EQ(data.size(), 0x40u + 4u + 1u + 2u + 2u + 4u + textSize) << "Dialog size mismatch";
}

TEST_F(SM64USIntegrationTest, TextBasic) {
    auto& data = GetAsset("test_text");
    ASSERT_FALSE(data.empty()) << "Text asset not found in output";

    // SM64:TEXT exports as Blob type
    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Blob));

    ASSERT_GE(data.size(), 0x44u) << "Text too small to contain size";

    uint32_t textSize;
    std::memcpy(&textSize, data.data() + 0x40, 4);
    EXPECT_GT(textSize, 0u) << "Expected non-empty text data";
    EXPECT_EQ(data.size(), 0x40u + 4u + textSize) << "Text size mismatch";
}

TEST_F(SM64USIntegrationTest, MacroBasic) {
    auto& data = GetAsset("test_macro");
    ASSERT_FALSE(data.empty()) << "Macro asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::MacroObject));

    // After 0x40 header: uint32 count, then count * int16
    ASSERT_GE(data.size(), 0x44u) << "Macro too small to contain count";

    uint32_t count;
    std::memcpy(&count, data.data() + 0x40, 4);
    EXPECT_GT(count, 0u) << "Expected at least one macro entry";
    EXPECT_EQ(data.size(), 0x40u + 4u + count * 2u) << "Macro size mismatch";
}

TEST_F(SM64USIntegrationTest, MovtexBasic) {
    auto& data = GetAsset("test_movtex");
    ASSERT_FALSE(data.empty()) << "Movtex asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Movtex));

    // After 0x40 header: uint32 size, then int16 data
    ASSERT_GE(data.size(), 0x44u) << "Movtex too small to contain size";

    uint32_t bufSize;
    std::memcpy(&bufSize, data.data() + 0x40, 4);
    EXPECT_GT(bufSize, 0u) << "Expected non-empty movtex data";
    EXPECT_EQ(data.size(), 0x40u + 4u + bufSize * 2u) << "Movtex size mismatch";
}

TEST_F(SM64USIntegrationTest, MovtexQuadBasic) {
    auto& data = GetAsset("test_movtex_quad");
    ASSERT_FALSE(data.empty()) << "MovtexQuad asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::MovtexQuad));

    // After 0x40 header: uint32 count, then (int16 id + uint64 hash) per quad
    ASSERT_GE(data.size(), 0x44u) << "MovtexQuad too small to contain count";

    uint32_t count;
    std::memcpy(&count, data.data() + 0x40, 4);
    EXPECT_EQ(count, 2u) << "Expected 2 movtex quads";
}

TEST_F(SM64USIntegrationTest, TrajectoryBasic) {
    auto& data = GetAsset("test_trajectory");
    ASSERT_FALSE(data.empty()) << "Trajectory asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Trajectory));

    // After 0x40 header: uint32 count, then count * (4x int16 = 8 bytes)
    ASSERT_GE(data.size(), 0x44u) << "Trajectory too small to contain count";

    uint32_t count;
    std::memcpy(&count, data.data() + 0x40, 4);
    EXPECT_GT(count, 0u) << "Expected at least one trajectory point";
    EXPECT_EQ(data.size(), 0x40u + 4u + count * 8u) << "Trajectory size mismatch";
}

TEST_F(SM64USIntegrationTest, PaintingBasic) {
    auto& data = GetAsset("test_painting");
    ASSERT_FALSE(data.empty()) << "Painting asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Painting));

    // Painting is a large fixed struct - just verify it's bigger than the header
    EXPECT_GT(data.size(), 0x40u + 20u) << "Painting too small";
}

TEST_F(SM64USIntegrationTest, PaintingMapBasic) {
    auto& data = GetAsset("test_painting_map");
    ASSERT_FALSE(data.empty()) << "PaintingMap asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::PaintingData));

    // After 0x40 header: uint32 total elements, then mappings and groups
    ASSERT_GE(data.size(), 0x44u) << "PaintingMap too small to contain count";

    uint32_t totalElements;
    std::memcpy(&totalElements, data.data() + 0x40, 4);
    EXPECT_GT(totalElements, 0u) << "Expected at least one painting map element";
}

TEST_F(SM64USIntegrationTest, DictionaryBasic) {
    auto& data = GetAsset("test_dictionary");
    ASSERT_FALSE(data.empty()) << "Dictionary asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Dictionary));

    // After 0x40 header: uint32 dict size, then key-value pairs
    ASSERT_GE(data.size(), 0x44u) << "Dictionary too small to contain size";

    uint32_t dictSize;
    std::memcpy(&dictSize, data.data() + 0x40, 4);
    EXPECT_EQ(dictSize, 3u) << "Expected 3 dictionary entries";
}

TEST_F(SM64USIntegrationTest, CollisionLevel) {
    auto& data = GetAsset("test_collision_level");
    ASSERT_FALSE(data.empty()) << "Level collision asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Collision));

    ASSERT_GE(data.size(), 0x44u) << "Collision too small to contain count";

    uint32_t cmdCount;
    std::memcpy(&cmdCount, data.data() + 0x40, 4);

    // Bob-omb Battlefield level collision is large — has vertices, surfaces,
    // special objects, and environment boxes
    EXPECT_GT(cmdCount, 100u) << "Expected a large collision command set";
    EXPECT_EQ(data.size(), 0x40u + 4u + cmdCount * 2u) << "Total size mismatch";
}

TEST_F(SM64USIntegrationTest, MovtexNonQuad) {
    auto& data = GetAsset("test_movtex_nonquad");
    ASSERT_FALSE(data.empty()) << "Non-quad movtex asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Movtex));

    ASSERT_GE(data.size(), 0x44u) << "Movtex too small to contain size";

    uint32_t bufSize;
    std::memcpy(&bufSize, data.data() + 0x40, 4);
    EXPECT_GT(bufSize, 0u) << "Expected non-empty movtex data";
    EXPECT_EQ(data.size(), 0x40u + 4u + bufSize * 2u) << "Movtex size mismatch";
}

TEST_F(SM64USIntegrationTest, MovtexNonQuadColor) {
    auto& data = GetAsset("test_movtex_nonquad_color");
    ASSERT_FALSE(data.empty()) << "Non-quad color movtex asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Movtex));

    ASSERT_GE(data.size(), 0x44u) << "Movtex too small to contain size";

    uint32_t bufSize;
    std::memcpy(&bufSize, data.data() + 0x40, 4);
    EXPECT_GT(bufSize, 0u) << "Expected non-empty movtex data";
    EXPECT_EQ(data.size(), 0x40u + 4u + bufSize * 2u) << "Movtex size mismatch";
}

TEST_F(SM64USIntegrationTest, CollisionWater) {
    auto& data = GetAsset("test_collision_water");
    ASSERT_FALSE(data.empty()) << "Water collision asset not found in output";

    ValidateHeader(data, static_cast<uint32_t>(Torch::ResourceType::Collision));

    ASSERT_GE(data.size(), 0x44u) << "Collision too small to contain count";

    uint32_t cmdCount;
    std::memcpy(&cmdCount, data.data() + 0x40, 4);

    // JRB area 1 collision should have water environment boxes
    EXPECT_GT(cmdCount, 100u) << "Expected a large collision command set";
    EXPECT_EQ(data.size(), 0x40u + 4u + cmdCount * 2u) << "Total size mismatch";
}

// Error handling tests: verify the pipeline doesn't crash on bad input
static const std::string SM64_US_ERROR_CONFIG_DIR = GetTestDir() + "/sm64/us_error";

TEST(SM64USErrorTest, BadInputDoesNotCrash) {
    if (!RomExists(SM64_US_ROM)) {
        GTEST_SKIP() << "SM64 US ROM not found";
    }
    auto romPath = GetRomDir() + "/" + SM64_US_ROM;

    // This runs the pipeline with YAMLs that have bad offsets, invalid formats, and missing fields.
    // We just verify it doesn't crash (SEGV/abort).
    EXPECT_NO_FATAL_FAILURE({
        try {
            auto assets = RunPipeline(SM64_US_ERROR_CONFIG_DIR, romPath);
        } catch (...) {
            // Any exception is fine - we just don't want crashes
        }
    });
}
