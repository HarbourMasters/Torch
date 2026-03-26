#include <gtest/gtest.h>
#include "utils/TorchUtils.h"
#include <map>
#include <set>

// to_hex
TEST(TorchUtilsTest, ToHexUint32WithPrefix) {
    // to_hex uppercases the entire output including the "0x" prefix
    EXPECT_EQ(Torch::to_hex<uint32_t>(0xDEADBEEF), "0XDEADBEEF");
}

TEST(TorchUtilsTest, ToHexUint32WithoutPrefix) {
    EXPECT_EQ(Torch::to_hex<uint32_t>(0xFF, false), "FF");
}

TEST(TorchUtilsTest, ToHexZero) {
    EXPECT_EQ(Torch::to_hex<uint32_t>(0), "0X0");
}

TEST(TorchUtilsTest, ToHexUint16) {
    EXPECT_EQ(Torch::to_hex<uint16_t>(0x1234), "0X1234");
}

TEST(TorchUtilsTest, ToHexUpperCase) {
    // Verify output is uppercase
    std::string result = Torch::to_hex<uint32_t>(0xabcdef);
    EXPECT_NE(result.find("ABCDEF"), std::string::npos);
}

// contains
TEST(TorchUtilsTest, ContainsMapKeyPresent) {
    std::map<std::string, int> m = {{"a", 1}, {"b", 2}};
    EXPECT_TRUE(Torch::contains(m, std::string("a")));
}

TEST(TorchUtilsTest, ContainsMapKeyAbsent) {
    std::map<std::string, int> m = {{"a", 1}, {"b", 2}};
    EXPECT_FALSE(Torch::contains(m, std::string("c")));
}

TEST(TorchUtilsTest, ContainsSetPresent) {
    std::set<int> s = {1, 2, 3};
    EXPECT_TRUE(Torch::contains(s, 2));
}

TEST(TorchUtilsTest, ContainsSetAbsent) {
    std::set<int> s = {1, 2, 3};
    EXPECT_FALSE(Torch::contains(s, 5));
}

TEST(TorchUtilsTest, ContainsEmptyContainer) {
    std::map<int, int> m;
    EXPECT_FALSE(Torch::contains(m, 0));
}
