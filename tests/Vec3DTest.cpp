#include <gtest/gtest.h>
#include "types/Vec3D.h"
#include <sstream>

// Vec3f construction
TEST(Vec3DTest, Vec3fConstruction) {
    Vec3f v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(Vec3DTest, Vec3fDefaultConstruction) {
    Vec3f v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

// Vec3s construction
TEST(Vec3DTest, Vec3sConstruction) {
    Vec3s v(100, -200, 300);
    EXPECT_EQ(v.x, 100);
    EXPECT_EQ(v.y, -200);
    EXPECT_EQ(v.z, 300);
}

TEST(Vec3DTest, Vec3sDefaultConstruction) {
    Vec3s v;
    EXPECT_EQ(v.x, 0);
    EXPECT_EQ(v.y, 0);
    EXPECT_EQ(v.z, 0);
}

// Vec3i construction
TEST(Vec3DTest, Vec3iConstruction) {
    Vec3i v(100000, -200000, 300000);
    EXPECT_EQ(v.x, 100000);
    EXPECT_EQ(v.y, -200000);
    EXPECT_EQ(v.z, 300000);
}

// Vec3iu construction
TEST(Vec3DTest, Vec3iuConstruction) {
    Vec3iu v(0xDEAD, 0xBEEF, 0xCAFE);
    EXPECT_EQ(v.x, 0xDEADu);
    EXPECT_EQ(v.y, 0xBEEFu);
    EXPECT_EQ(v.z, 0xCAFEu);
}

// Vec2f construction — note: second field is `z`, not `y`
TEST(Vec3DTest, Vec2fConstruction) {
    Vec2f v(1.5f, 2.5f);
    EXPECT_FLOAT_EQ(v.x, 1.5f);
    EXPECT_FLOAT_EQ(v.z, 2.5f);
}

// Vec4f construction
TEST(Vec3DTest, Vec4fConstruction) {
    Vec4f v(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
    EXPECT_FLOAT_EQ(v.w, 4.0f);
}

// Vec4s construction
TEST(Vec3DTest, Vec4sConstruction) {
    Vec4s v(10, 20, 30, 40);
    EXPECT_EQ(v.x, 10);
    EXPECT_EQ(v.y, 20);
    EXPECT_EQ(v.z, 30);
    EXPECT_EQ(v.w, 40);
}

// Precision tests
TEST(Vec3DTest, Vec3fPrecisionWholeNumbers) {
    Vec3f v(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(v.precision(), 0);
}

TEST(Vec3DTest, Vec3fPrecisionOneDecimal) {
    Vec3f v(1.5f, 2.0f, 3.0f);
    EXPECT_EQ(v.precision(), 1);
}

TEST(Vec3DTest, Vec3fPrecisionMaxOfComponents) {
    // precision returns the max precision across all components
    Vec3f v(1.0f, 2.5f, 3.25f);
    EXPECT_EQ(v.precision(), 2);
}

// Width tests
TEST(Vec3DTest, Vec3fWidthWholeNumbers) {
    // width = max_magnitude + 1 + precision
    // (1.0, 2.0, 3.0): max magnitude is 1 (for 3.0), precision is 0
    // width = 1 + 1 + 0 = 2
    Vec3f v(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(v.width(), 2);
}

TEST(Vec3DTest, Vec3sWidth) {
    // width = max_magnitude (no precision for integers)
    // (100, -200, 300): magnitudes are 3, 4 (neg adds 1), 3 → max = 4
    Vec3s v(100, -200, 300);
    EXPECT_EQ(v.width(), 4);
}

TEST(Vec3DTest, Vec3sWidthZeros) {
    Vec3s v(0, 0, 0);
    EXPECT_EQ(v.width(), 1);
}

TEST(Vec3DTest, Vec4sWidth) {
    Vec4s v(1, 10, 100, 1000);
    // magnitudes: 1, 2, 3, 4 → max = 4
    EXPECT_EQ(v.width(), 4);
}

// Stream output tests
TEST(Vec3DTest, Vec3fStreamOutput) {
    Vec3f v(1.0f, 2.0f, 3.0f);
    std::ostringstream oss;
    oss << v;
    std::string result = oss.str();
    EXPECT_NE(result.find("1"), std::string::npos);
    EXPECT_NE(result.find("2"), std::string::npos);
    EXPECT_NE(result.find("3"), std::string::npos);
    EXPECT_NE(result.find("{"), std::string::npos);
    EXPECT_NE(result.find("}"), std::string::npos);
}

TEST(Vec3DTest, Vec3sStreamOutput) {
    Vec3s v(10, 20, 30);
    std::ostringstream oss;
    oss << v;
    std::string result = oss.str();
    EXPECT_NE(result.find("10"), std::string::npos);
    EXPECT_NE(result.find("20"), std::string::npos);
    EXPECT_NE(result.find("30"), std::string::npos);
}

TEST(Vec3DTest, Vec2fStreamOutput) {
    Vec2f v(1.5f, 2.5f);
    std::ostringstream oss;
    oss << v;
    std::string result = oss.str();
    EXPECT_NE(result.find("1.5"), std::string::npos);
    EXPECT_NE(result.find("2.5"), std::string::npos);
}

TEST(Vec3DTest, Vec4fStreamOutput) {
    Vec4f v(1.0f, 2.0f, 3.0f, 4.0f);
    std::ostringstream oss;
    oss << v;
    std::string result = oss.str();
    // Should contain all 4 values and braces
    EXPECT_NE(result.find("{"), std::string::npos);
    EXPECT_NE(result.find("}"), std::string::npos);
}
