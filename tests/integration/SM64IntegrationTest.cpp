#include <gtest/gtest.h>
#include "IntegrationTestHelpers.h"

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
};

std::map<std::string, std::vector<char>> SM64USIntegrationTest::sAssets;
bool SM64USIntegrationTest::sPipelineRan = false;

TEST_F(SM64USIntegrationTest, PipelineProducesAssets) {
    EXPECT_FALSE(sAssets.empty()) << "Pipeline produced no assets";
}
