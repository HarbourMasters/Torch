cmake_minimum_required(VERSION 3.12)
project(torch)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

# Link libgfxd
# Because libgfxd is not a CMake project, we have to manually fetch it and add it to the build

FetchContent_Declare(
    libgfxd
    GIT_REPOSITORY https://github.com/glankk/libgfxd.git
    GIT_TAG 96fd3b849f38b3a7c7b7f3ff03c5921d328e6cdf
)

FetchContent_GetProperties(libgfxd)

if(NOT libgfxd_POPULATED)
    FetchContent_Populate(libgfxd)
    include_directories(${libgfxd_SOURCE_DIR})
    set(LGFXD_SRC gfxd.c uc_f3d.c uc_f3db.c uc_f3dex.c uc_f3dexb.c uc_f3dex2.c)
    foreach (LGFXD_FILE ${LGFXD_SRC})
        list(APPEND LGFXD_FILES "${libgfxd_SOURCE_DIR}/${LGFXD_FILE}")
    endforeach()
endif()

# Source files

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE CXX_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/lib/strhash64/*.cpp)
file(GLOB C_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lib/**/*.c)
set(SRC_DIR ${CXX_FILES} ${C_FILES} ${LGFXD_FILES})

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

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    set(CMAKE_CXX_FLAGS "-Wno-narrowing")
else()
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /w;
                /Od;
                /MDd
            >
            $<$<CONFIG:Release>:
                /Oi;
                /Gy;
                /W3;
                /MD
            >
            /permissive-;
            /MP;
            ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
            ${DEFAULT_CXX_EXCEPTION_HANDLING}
        )
        target_compile_options(${PROJECT_NAME} PRIVATE  $<$<CONFIG:Debug>:/ZI;>)
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /MDd
            >
            $<$<CONFIG:Release>:
                /O2;
                /Oi;
                /Gy;
                /MD
            >
            /permissive-;
            /MP;
            /w;
            ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
            ${DEFAULT_CXX_EXCEPTION_HANDLING}
        )
        endif()
    if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /INCREMENTAL
            >
            $<$<CONFIG:Release>:
                /OPT:REF;
                /OPT:ICF;
                /INCREMENTAL:NO;
                /FORCE:MULTIPLE
            >
            /MANIFEST:NO;
            /DEBUG;
            /SUBSYSTEM:CONSOLE
        )
    elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
        target_link_options(${PROJECT_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /STACK:8777216
            >
            $<$<CONFIG:Release>:
                /OPT:REF;
                /OPT:ICF;
                /INCREMENTAL:NO;
                /FORCE:MULTIPLE
            >
            /MANIFEST:NO;
            /DEBUG;
            /SUBSYSTEM:CONSOLE
        )
    endif()
endif()

# Fetch Dependencies

if(EXISTS "/mnt/c/WINDOWS/system32/wsl.exe")
FetchContent_Declare(
    GSL
    GIT_REPOSITORY https://github.com/Microsoft/GSL.git
    GIT_TAG a3534567187d2edc428efd3f13466ff75fe5805c
    )
    FetchContent_MakeAvailable(GSL)
    target_link_libraries(${PROJECT_NAME} PRIVATE GSL)
    endif()

# Link BinaryTools

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/binarytools)
add_dependencies(${PROJECT_NAME} BinaryTools)
target_link_libraries(${PROJECT_NAME} PRIVATE BinaryTools)

# Link n64graphics

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/n64graphics)
add_dependencies(${PROJECT_NAME} N64Graphics)
target_link_libraries(${PROJECT_NAME} PRIVATE N64Graphics)

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