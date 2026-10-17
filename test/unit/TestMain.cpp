#include <gtest/gtest.h>

#include "Companion.h"
#include "spdlog/spdlog.h"

Companion* Companion::Instance = nullptr;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    spdlog::set_level(spdlog::level::err);
    return RUN_ALL_TESTS();
}
