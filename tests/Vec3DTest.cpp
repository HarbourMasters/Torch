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
