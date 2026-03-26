#include <gtest/gtest.h>
#include "Companion.h"
#include "factories/VtxFactory.h"
#include "factories/MtxFactory.h"
#include "factories/FloatFactory.h"

class CompanionRegistrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Construct Companion with empty ROM, no archive, no debug
        std::vector<uint8_t> emptyRom;
        companion = std::make_unique<Companion>(emptyRom, ArchiveType::None, false, false);
    }

    std::unique_ptr<Companion> companion;
};

TEST_F(CompanionRegistrationTest, RegisterAndGetFactory) {
    auto factory = std::make_shared<VtxFactory>();
    companion->RegisterFactory("TEST_VTX", factory);
    auto result = companion->GetFactory("TEST_VTX");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().get(), factory.get());
}

TEST_F(CompanionRegistrationTest, GetFactoryMissing) {
    auto result = companion->GetFactory("NONEXISTENT");
    EXPECT_FALSE(result.has_value());
}

TEST_F(CompanionRegistrationTest, RegisterMultipleFactories) {
    companion->RegisterFactory("A", std::make_shared<VtxFactory>());
    companion->RegisterFactory("B", std::make_shared<MtxFactory>());
    companion->RegisterFactory("C", std::make_shared<FloatFactory>());

    EXPECT_TRUE(companion->GetFactory("A").has_value());
    EXPECT_TRUE(companion->GetFactory("B").has_value());
    EXPECT_TRUE(companion->GetFactory("C").has_value());
    EXPECT_FALSE(companion->GetFactory("D").has_value());
}

TEST_F(CompanionRegistrationTest, RegisterOverwritesPrevious) {
    auto factory1 = std::make_shared<VtxFactory>();
    auto factory2 = std::make_shared<MtxFactory>();
    companion->RegisterFactory("SAME", factory1);
    companion->RegisterFactory("SAME", factory2);
    auto result = companion->GetFactory("SAME");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().get(), factory2.get());
}

TEST_F(CompanionRegistrationTest, FactoryExportersAccessible) {
    auto factory = std::make_shared<VtxFactory>();
    companion->RegisterFactory("VTX", factory);
    auto result = companion->GetFactory("VTX");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value()->GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(result.value()->GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(result.value()->GetExporter(ExportType::Binary).has_value());
}

TEST_F(CompanionRegistrationTest, FactoryAlignmentAccessible) {
    auto factory = std::make_shared<VtxFactory>();
    companion->RegisterFactory("VTX", factory);
    auto result = companion->GetFactory("VTX");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->GetAlignment(), 8u);
}
