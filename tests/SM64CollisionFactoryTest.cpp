#include <gtest/gtest.h>
#include "factories/sm64/CollisionFactory.h"
#include <vector>

using namespace SM64;

TEST(SM64CollisionFactoryTest, CollisionVertexConstruction) {
    CollisionVertex v(100, -200, 300);
    EXPECT_EQ(v.x, 100);
    EXPECT_EQ(v.y, -200);
    EXPECT_EQ(v.z, 300);
}

TEST(SM64CollisionFactoryTest, CollisionTriConstruction) {
    CollisionTri tri(0, 1, 2, 50);
    EXPECT_EQ(tri.x, 0);
    EXPECT_EQ(tri.y, 1);
    EXPECT_EQ(tri.z, 2);
    EXPECT_EQ(tri.force, 50);
}

TEST(SM64CollisionFactoryTest, CollisionTriNoForce) {
    CollisionTri tri(3, 4, 5, 0);
    EXPECT_EQ(tri.force, 0);
}

TEST(SM64CollisionFactoryTest, CollisionSurfaceConstruction) {
    std::vector<CollisionTri> tris = {
        {0, 1, 2, 0},
        {2, 1, 3, 0},
    };
    CollisionSurface surface(SurfaceType::SURFACE_DEFAULT, tris);
    EXPECT_EQ(surface.surfaceType, SurfaceType::SURFACE_DEFAULT);
    ASSERT_EQ(surface.tris.size(), 2u);
    EXPECT_EQ(surface.tris[0].x, 0);
    EXPECT_EQ(surface.tris[1].z, 3);
}

TEST(SM64CollisionFactoryTest, SpecialObjectConstruction) {
    std::vector<int16_t> params = {90};
    SpecialObject obj(SpecialPresets::special_yellow_coin, 100, 200, 300, params);
    EXPECT_EQ(obj.presetId, SpecialPresets::special_yellow_coin);
    EXPECT_EQ(obj.x, 100);
    EXPECT_EQ(obj.y, 200);
    EXPECT_EQ(obj.z, 300);
    ASSERT_EQ(obj.extraParams.size(), 1u);
    EXPECT_EQ(obj.extraParams[0], 90);
}

TEST(SM64CollisionFactoryTest, SpecialObjectNoExtraParams) {
    std::vector<int16_t> params;
    SpecialObject obj(SpecialPresets::special_null_start, 0, 0, 0, params);
    EXPECT_TRUE(obj.extraParams.empty());
}

TEST(SM64CollisionFactoryTest, EnvRegionBoxConstruction) {
    EnvRegionBox box(1, -1000, -2000, 1000, 2000, 500);
    EXPECT_EQ(box.id, 1);
    EXPECT_EQ(box.x1, -1000);
    EXPECT_EQ(box.z1, -2000);
    EXPECT_EQ(box.x2, 1000);
    EXPECT_EQ(box.z2, 2000);
    EXPECT_EQ(box.height, 500);
}

TEST(SM64CollisionFactoryTest, CollisionFullConstruction) {
    std::vector<CollisionVertex> verts = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}};
    std::vector<CollisionTri> tris = {{0, 1, 2, 0}};
    std::vector<CollisionSurface> surfaces = {{SurfaceType::SURFACE_DEFAULT, tris}};
    std::vector<SpecialObject> specials;
    std::vector<EnvRegionBox> boxes;

    Collision col(verts, surfaces, specials, boxes);
    ASSERT_EQ(col.mVertices.size(), 3u);
    ASSERT_EQ(col.mSurfaces.size(), 1u);
    EXPECT_TRUE(col.mSpecialObjects.empty());
    EXPECT_TRUE(col.mEnvRegionBoxes.empty());
    EXPECT_EQ(col.mVertices[1].x, 100);
    EXPECT_EQ(col.mSurfaces[0].surfaceType, SurfaceType::SURFACE_DEFAULT);
}

TEST(SM64CollisionFactoryTest, HasExpectedExporters) {
    CollisionFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Code));
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}
