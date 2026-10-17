#include <gtest/gtest.h>

#include "Companion.h"
#include "TestUtils.h"

using namespace TestUtils;

TEST(Manifest, ListsDeclaredAssetsWithoutAnInput) {
    const auto root = MakeTempDir("manifest");
    WriteText(root / "data" / "field" / "obj.yml", ":config:\n"
                                                    "  strict_declarations: true\n"
                                                    "obj_tex:\n"
                                                    "  type: TEXTURE\n"
                                                    "  offset: 0x81000000\n"
                                                    "  format: CI4\n"
                                                    "  width: 16\n"
                                                    "  height: 16\n"
                                                    "obj_dl:\n"
                                                    "  type: GFX\n"
                                                    "  offset: 0x81000100\n"
                                                    "  size: 0x10\n"
                                                    "obj_hidden:\n"
                                                    "  type: BLOB\n"
                                                    "  offset: 0x81000200\n"
                                                    "  size: 4\n"
                                                    "  no_export: true\n");
    WriteText(root / "moved.yml", ":config:\n"
                                  "  directory: scenes/room\n"
                                  "room_vtx:\n"
                                  "  type: VTX\n"
                                  "  offset: 0x81000300\n"
                                  "  count: 4\n");
    WriteText(root / "config.yml", "ignored: { type: BLOB }\n");

    const std::vector<std::string> expected = { "data/field/obj/obj_dl", "data/field/obj/obj_tex",
                                                 "scenes/room/room_vtx" };
    EXPECT_EQ(Companion::Manifest(root, {}), expected);

    const std::vector<std::string> single = { "scenes/room/room_vtx" };
    EXPECT_EQ(Companion::Manifest(root, { "moved.yml" }), single);
    EXPECT_EQ(Companion::Manifest(root, { (root / "data").string() }).size(), 2u);

    std::filesystem::remove_all(root);
}
