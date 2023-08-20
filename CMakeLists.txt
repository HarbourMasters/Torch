cmake_minimum_required(VERSION 3.12)
project(CubeOS)

set(CMAKE_CXX_STANDARD 20)

# Source files

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB CXX_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp)
file(GLOB C_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lib/**/*.c)
set(SRC_DIR ${CXX_FILES} ${C_FILES})

set(VPKTOOL_BUILD_GUI OFF)
set(VPKTOOL_BUILD_TESTS OFF)
set(VPKTOOL_BUILD_INSTALLER OFF)

# Build
add_executable(${PROJECT_NAME} ${SRC_DIR})

# Fetch Dependencies
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/binarytools)

# Link BinaryTools
add_dependencies(${PROJECT_NAME} BinaryTools)
target_link_libraries(${PROJECT_NAME} PRIVATE BinaryTools)

# Link StormLib

set(STORMLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/StormLib)
add_subdirectory(${STORMLIB_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE storm)

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