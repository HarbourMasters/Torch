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
        for (auto& [key, value] : sAssets) {
            if (key.find(name) != std::string::npos) {
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
