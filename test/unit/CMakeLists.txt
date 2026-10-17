include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(TORCH_TEST_SOURCES ${SRC_DIR})
list(FILTER TORCH_TEST_SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")

set(TORCH_UNIT_TESTS
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ManifestTests.cpp
)

if(BUILD_GAMECUBE OR BUILD_AC)
    list(APPEND TORCH_UNIT_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/GXTextureTests.cpp)
endif()

if(BUILD_GAMECUBE)
    list(APPEND TORCH_UNIT_TESTS
        ${CMAKE_CURRENT_SOURCE_DIR}/SyntheticDisc.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/RelModuleTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/RarcTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/DiscTests.cpp
    )
endif()

if(BUILD_AC)
    list(APPEND TORCH_UNIT_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/ACDListTests.cpp)
endif()

if(BUILD_GAMECUBE AND BUILD_AC)
    list(APPEND TORCH_UNIT_TESTS
        ${CMAKE_CURRENT_SOURCE_DIR}/GameCubeExtractTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ACFactoryTests.cpp
    )
endif()

add_executable(torch_tests ${TORCH_TEST_SOURCES} ${TORCH_UNIT_TESTS})
target_include_directories(torch_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(torch_tests PRIVATE
    GTest::gtest
    tinyxml2
    ${TORCH_ZLIB_TARGET}
    yaml-cpp
    N64Graphics
    BinaryTools
    spdlog::spdlog
)
add_dependencies(torch_tests BinaryTools N64Graphics)

include(GoogleTest)
gtest_discover_tests(torch_tests DISCOVERY_TIMEOUT 60)
