#include <gtest/gtest.h>
#include "utils/StringHelper.h"

// Split
TEST(StringHelperTest, SplitBasic) {
    auto result = StringHelper::Split(std::string("a,b,c"), ",");
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
}

TEST(StringHelperTest, SplitNoDelimiter) {
    auto result = StringHelper::Split(std::string("hello"), ",");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "hello");
}

TEST(StringHelperTest, SplitEmptyString) {
    auto result = StringHelper::Split(std::string(""), ",");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "");
}

TEST(StringHelperTest, SplitMultiCharDelimiter) {
    auto result = StringHelper::Split(std::string("a::b::c"), "::");
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
}

// Replace
TEST(StringHelperTest, ReplaceBasic) {
    EXPECT_EQ(StringHelper::Replace("hello world", "world", "there"), "hello there");
}

TEST(StringHelperTest, ReplaceNotFound) {
    EXPECT_EQ(StringHelper::Replace("hello", "xyz", "abc"), "hello");
}

TEST(StringHelperTest, ReplaceMultipleOccurrences) {
    EXPECT_EQ(StringHelper::Replace("aaa", "a", "bb"), "bbbbbb");
}

// ReplaceOriginal (in-place)
TEST(StringHelperTest, ReplaceOriginalModifiesString) {
    std::string s = "foo bar foo";
    StringHelper::ReplaceOriginal(s, "foo", "baz");
    EXPECT_EQ(s, "baz bar baz");
}

// StartsWith / EndsWith / Contains
TEST(StringHelperTest, StartsWithTrue) {
    EXPECT_TRUE(StringHelper::StartsWith("hello world", "hello"));
}

TEST(StringHelperTest, StartsWithFalse) {
    EXPECT_FALSE(StringHelper::StartsWith("hello world", "world"));
}

TEST(StringHelperTest, EndsWithTrue) {
    EXPECT_TRUE(StringHelper::EndsWith("hello world", "world"));
}

TEST(StringHelperTest, EndsWithFalse) {
    EXPECT_FALSE(StringHelper::EndsWith("hello world", "hello"));
}

TEST(StringHelperTest, ContainsTrue) {
    EXPECT_TRUE(StringHelper::Contains("hello world", "lo wo"));
}

TEST(StringHelperTest, ContainsFalse) {
    EXPECT_FALSE(StringHelper::Contains("hello world", "xyz"));
}

// IEquals
TEST(StringHelperTest, IEqualsSameCase) {
    EXPECT_TRUE(StringHelper::IEquals("hello", "hello"));
}

TEST(StringHelperTest, IEqualsDifferentCase) {
    EXPECT_TRUE(StringHelper::IEquals("Hello", "hELLO"));
}

TEST(StringHelperTest, IEqualsNotEqual) {
    EXPECT_FALSE(StringHelper::IEquals("hello", "world"));
}

// HasOnlyDigits
TEST(StringHelperTest, HasOnlyDigitsTrue) {
    EXPECT_TRUE(StringHelper::HasOnlyDigits("12345"));
}

TEST(StringHelperTest, HasOnlyDigitsFalse) {
    EXPECT_FALSE(StringHelper::HasOnlyDigits("123a5"));
}

TEST(StringHelperTest, HasOnlyDigitsEmpty) {
    // std::all_of on empty range returns true
    EXPECT_TRUE(StringHelper::HasOnlyDigits(""));
}

// IsValidHex
TEST(StringHelperTest, IsValidHexLowerCase) {
    EXPECT_TRUE(StringHelper::IsValidHex(std::string("0xABCD")));
}

TEST(StringHelperTest, IsValidHexUpperX) {
    EXPECT_TRUE(StringHelper::IsValidHex(std::string("0X1f")));
}

TEST(StringHelperTest, IsValidHexNoPrefix) {
    EXPECT_FALSE(StringHelper::IsValidHex(std::string("ABCD")));
}

TEST(StringHelperTest, IsValidHexTooShort) {
    EXPECT_FALSE(StringHelper::IsValidHex(std::string("0x")));
}

TEST(StringHelperTest, IsValidHexInvalidChars) {
    EXPECT_FALSE(StringHelper::IsValidHex(std::string("0xGHIJ")));
}

// IsValidOffset
TEST(StringHelperTest, IsValidOffsetSingleDigit) {
    EXPECT_TRUE(StringHelper::IsValidOffset(std::string("0")));
    EXPECT_TRUE(StringHelper::IsValidOffset(std::string("5")));
}

TEST(StringHelperTest, IsValidOffsetHex) {
    EXPECT_TRUE(StringHelper::IsValidOffset(std::string("0x100")));
}

TEST(StringHelperTest, IsValidOffsetInvalid) {
    EXPECT_FALSE(StringHelper::IsValidOffset(std::string("abc")));
}

// StrToL
TEST(StringHelperTest, StrToLDecimal) {
    EXPECT_EQ(StringHelper::StrToL("42"), 42);
}

TEST(StringHelperTest, StrToLHex) {
    EXPECT_EQ(StringHelper::StrToL("FF", 16), 255);
}

// BoolStr
TEST(StringHelperTest, BoolStrTrue) {
    EXPECT_EQ(StringHelper::BoolStr(true), "true");
}

TEST(StringHelperTest, BoolStrFalse) {
    EXPECT_EQ(StringHelper::BoolStr(false), "false");
}

// Sprintf
TEST(StringHelperTest, SprintfBasic) {
    EXPECT_EQ(StringHelper::Sprintf("hello %s %d", "world", 42), "hello world 42");
}
