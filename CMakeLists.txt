cmake_minimum_required(VERSION 3.12)
project(CubeOS)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)

# Source files

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE CXX_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/lib/strhash64/*.cpp)
file(GLOB C_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lib/**/*.c)
set(SRC_DIR ${CXX_FILES} ${C_FILES})
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(USE_STANDALONE ON)
set(VPKTOOL_BUILD_GUI OFF)
set(VPKTOOL_BUILD_TESTS OFF)
set(VPKTOOL_BUILD_INSTALLER OFF)

# Build
if (USE_STANDALONE)
    add_definitions(-DSTANDALONE)
    add_executable(${PROJECT_NAME} ${SRC_DIR})
else()
    add_library(${PROJECT_NAME} SHARED ${SRC_DIR})
endif()

# Fetch Dependencies
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/binarytools)

# Link BinaryTools
add_dependencies(${PROJECT_NAME} BinaryTools)
target_link_libraries(${PROJECT_NAME} PRIVATE BinaryTools)

# Link StormLib

set(STORMLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/StormLib)
add_subdirectory(${STORMLIB_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE storm)

# Link YamlCpp

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG f7320141120f720aecc4c32be25586e7da9eb978
)

set(YAML_CPP_BUILD_TESTS OFF)
FetchContent_MakeAvailable(yaml-cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE yaml-cpp)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG 7e635fca68d014934b4af8a1cf874f63989352b7
)

FetchContent_MakeAvailable(spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog)

if((CMAKE_SYSTEM_NAME MATCHES "Windows") AND ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    include(../cmake/HandleCompilerRT.cmake)
    find_compiler_rt_library(builtins CLANG_RT_BUILTINS_LIBRARY)
    get_filename_component(LIBDIR "${CLANG_RT_BUILTINS_LIBRARY}" DIRECTORY)
    if(IS_DIRECTORY "${LIBDIR}")
        target_link_libraries(storm ${CLANG_RT_BUILTINS_LIBRARY})
    endif()
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    target_compile_definitions(storm PRIVATE -D_POSIX_C_SOURCE=200809L)
endif()