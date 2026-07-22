cmake_minimum_required(VERSION 3.12)
project(torch)
include(FetchContent)

set(CMAKE_POLICY_VERSION_MINIMUM 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# Set build options
option(USE_STANDALONE "Build as a standalone executable" ON)
option(BUILD_STORMLIB "Build with StormLib support" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

option(BUILD_SM64 "Build with Super Mario 64 support" ON)
option(BUILD_MK64 "Build with Mario Kart 64 support" ON)
option(BUILD_SF64 "Build with Star Fox 64 support" ON)
option(BUILD_PM64 "Build with Paper Mario support" ON)
option(BUILD_FZERO "Build with F-Zero X support" ON)
option(BUILD_BK64 "Build with Banjo Kazooie support" ON)
option(BUILD_MARIO_ARTIST "Build with Mario Artist support" ON)
option(BUILD_NAUDIO "Build with NAudio support" ON)

option(BUILD_UI "Build with the interactive viewer (libultraship + Fast3D + ImGui)" OFF)

if(EMSCRIPTEN)
    add_definitions(-D__EMSCRIPTEN__)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --bind -lidbfs.js -s MODULARIZE=1 -s EXPORT_ES6=1 -s NO_DISABLE_EXCEPTION_CATCHING -s ALLOW_MEMORY_GROWTH=1 -s EXPORT_NAME=createModule -s EXPORTED_RUNTIME_METHODS='[\"FS\", \"IDBFS\"]'")
    if(EMSCRIPTEN_PRELOAD)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --preload-file ${EMSCRIPTEN_PRELOAD}")
    endif()
endif()

################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
    message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

    if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64"
        OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"))
        message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
    endif()
endif()

if(USE_STANDALONE)
    # Link libgfxd
    # Because libgfxd is not a CMake project, we have to manually fetch it and add it to the build
    FetchContent_Declare(
        libgfxd
        GIT_REPOSITORY https://github.com/glankk/libgfxd.git
        GIT_TAG 96fd3b849f38b3a7c7b7f3ff03c5921d328e6cdf
    )

    FetchContent_GetProperties(libgfxd)

    if(NOT libgfxd_POPULATED)
        FetchContent_MakeAvailable(libgfxd)
        include_directories(${libgfxd_SOURCE_DIR})
        set(LGFXD_SRC gfxd.c uc_f3d.c uc_f3db.c uc_f3dex.c uc_f3dexb.c uc_f3dex2.c)
        foreach (LGFXD_FILE ${LGFXD_SRC})
            list(APPEND LGFXD_FILES "${libgfxd_SOURCE_DIR}/${LGFXD_FILE}")
        endforeach()
    endif()
endif()

# Interactive viewer renderer: libultraship + Fast3D (true N64 rendering). LUS
# bundles its own ImGui, so it fully replaces the old raylib/rlImGui backend.
if(BUILD_UI)
    add_definitions(-DBUILD_UI)

    # libultraship integration. Recipe mirrors the Ghostship port.
    set(EXCLUDE_MPQ_SUPPORT TRUE CACHE BOOL "")
    add_compile_definitions(EXCLUDE_MPQ_SUPPORT)
    set(ENABLE_SCRIPTING OFF CACHE BOOL "" FORCE)

    # tinyxml2 must resolve before LUS's find_package(tinyxml2 REQUIRED).
    set(tinyxml2_BUILD_TESTING OFF)
    FetchContent_Declare(
        tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 10.0.0
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(tinyxml2)

    FetchContent_Declare(
        dr_libs
        GIT_REPOSITORY https://github.com/mackron/dr_libs.git
        GIT_TAG da35f9d6c7374a95353fd1df1d394d44ab66cf01
    )
    FetchContent_MakeAvailable(dr_libs)

    # Kenix3 upstream for now. NOTE: the HM64 ports build against a fork
    # (KiritoDv/better-mipmaps @ 5959b97) with fixes not on upstream; re-pin here
    # if upstream proves incompatible.
    FetchContent_Declare(
        libultraship
        GIT_REPOSITORY https://github.com/Kenix3/libultraship.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(libultraship)

    include_directories(
        ${libultraship_SOURCE_DIR}/include
        ${libultraship_SOURCE_DIR}/include/libultraship
        ${libultraship_SOURCE_DIR}/src
        ${dr_libs_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
    )

    # The Fast3D Metal/GL shaders are loaded as resources ("shaders/...") and must
    # match THIS libultraship's prism context. Game .o2r archives ship their own
    # (often fork-modified) shaders, so copy the upstream LUS shaders next to the
    # build and mount them last (overriding) at runtime. See LusBackend.cpp.
    file(COPY ${libultraship_SOURCE_DIR}/src/fast/shaders
         DESTINATION ${CMAKE_BINARY_DIR}/torch-lus-assets)
    add_compile_definitions(TORCH_LUS_SHADER_DIR="${CMAKE_BINARY_DIR}/torch-lus-assets")
endif()

# Source files

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE CXX_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/lib/strhash64/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/lib/bk_zip/*.cpp)
file(GLOB C_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lib/libmio0/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lib/libyay0/*.c)
set(SRC_DIR ${CXX_FILES} ${C_FILES} ${LGFXD_FILES} ${UI_FILES})

if(BUILD_UI)
    # libultraship bundles StringHelper and StrHash64 (shared origin with Torch).
    # Linking it WHOLE_ARCHIVE pulls those in, so drop our copies and resolve
    # against LUS's (a superset) to avoid duplicate-symbol link errors.
    list(FILTER SRC_DIR EXCLUDE REGEX "src/utils/StringHelper.cpp")
    list(FILTER SRC_DIR EXCLUDE REGEX "lib/strhash64/StrHash64.cpp")
endif()

if(BUILD_SM64)
    add_definitions(-DSM64_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/sm64/*")
endif()

if(BUILD_SF64)
    add_definitions(-DSF64_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/sf64/*")
endif()

if(BUILD_MK64)
    add_definitions(-DMK64_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/mk64/*")
endif()

if(BUILD_PM64)
    add_definitions(-DPM64_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/pm64/*")
endif()

if(BUILD_FZERO)
    add_definitions(-DFZERO_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/fzerox/*")
endif()

if(BUILD_BK64)
    add_definitions(-DBK64_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/bk64/*")
endif()

if(BUILD_MARIO_ARTIST)
    add_definitions(-DMARIO_ARTIST_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/mario_artist/*")
endif()

if(BUILD_NAUDIO)
    add_definitions(-DNAUDIO_SUPPORT)
else()
    list(FILTER SRC_DIR EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/factories/naudio/*")
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

# MSVC static runtime for all targets (must be set before add_subdirectory/FetchContent)
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # NOMINMAX: keep windows.h from defining min/max macros that break std::min/std::max.
    # _USE_MATH_DEFINES: expose M_PI and friends from <cmath>. (Covers clang-cl too.)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()

# Build
if (USE_STANDALONE)
    add_definitions(-DSTANDALONE)
    add_executable(${PROJECT_NAME} ${SRC_DIR})
	set(LINK_TYPE "MT")
else()
    add_library(${PROJECT_NAME} STATIC ${SRC_DIR})
	set(LINK_TYPE "MT")
endif()

if (BUILD_STORMLIB)
    add_definitions(-DUSE_STORMLIB)
endif()

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    set(CMAKE_CXX_FLAGS "-Wno-narrowing")
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
            /w;
            /Od;
            $<IF:$<BOOL:${ENABLE_ASAN}>,/Zi,/ZI>;
            /${LINK_TYPE}d
        >
        $<$<CONFIG:Release>:
            /O3;
            /Gy;
            /W3;
            /${LINK_TYPE}
        >
        /permissive-;
        /MP;
        ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
        ${DEFAULT_CXX_EXCEPTION_HANDLING}
    )
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
    if (NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
        target_link_options(${PROJECT_NAME} PRIVATE /bigobj)
    else()
        # clang-cl treats C++11 narrowing as a hard error by default (MSVC and
        # GCC only warn); mirror the -Wno-narrowing used for non-MSVC builds.
        target_compile_options(${PROJECT_NAME} PRIVATE -Wno-narrowing)
    endif()
    add_definitions(-DSTORMLIB_NO_AUTO_LINK)
endif()


# Fetch Dependencies

if(NOT USE_STANDALONE AND EXISTS "/mnt/c/WINDOWS/system32/wsl.exe")
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

# Link n64graphics

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/n64graphics)
add_dependencies(${PROJECT_NAME} N64Graphics)

# Link StormLib
if (BUILD_STORMLIB)
    set(STORM_BUILD_TESTS OFF)
    set(STORMLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/StormLib)
    add_subdirectory(${STORMLIB_DIR})
    if(USE_STANDALONE)
        if((CMAKE_SYSTEM_NAME MATCHES "Windows") AND ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
            include(../cmake/HandleCompilerRT.cmake)
            find_compiler_rt_library(builtins CLANG_RT_BUILTINS_LIBRARY)
            get_filename_component(LIBDIR "${CLANG_RT_BUILTINS_LIBRARY}" DIRECTORY)
            if(USE_STANDALONE AND IS_DIRECTORY "${LIBDIR}")
                target_link_libraries(storm ${CLANG_RT_BUILTINS_LIBRARY})
            endif()
        else()
            target_link_libraries(${PROJECT_NAME} PRIVATE storm)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
            target_compile_definitions(storm PRIVATE -D_POSIX_C_SOURCE=200809L)
        endif()
    endif ()
endif()

# Link YamlCpp

set(YAML_CPP_BUILD_TOOLS OFF)
set(YAML_CPP_BUILD_TESTS OFF)
set(YAML_CPP_DISABLE_UNINSTALL ON)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 2f86d13775d119edbb69af52e5f566fd65c6953b
)
set(YAML_CPP_BUILD_TESTS OFF)
FetchContent_MakeAvailable(yaml-cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE yaml-cpp)
if(NOT USE_STANDALONE)
	target_compile_definitions(yaml-cpp PRIVATE YAML_CPP_STATIC_DEFINE)
endif()

if(USE_STANDALONE)
    if(EMSCRIPTEN)
        set(CMAKE_CXX_FLAGS_DEBUG "-O3")
        set(SPDLOG_USE_STD_FORMAT ON)
    endif()

	FetchContent_Declare(
		spdlog
		GIT_REPOSITORY https://github.com/gabime/spdlog.git
		GIT_TAG 79524ddd08a4ec981b7fea76afd08ee05f83755d
	)

	FetchContent_MakeAvailable(spdlog)
	target_link_libraries(${PROJECT_NAME} PRIVATE spdlog)

	if (MSVC)
		FetchContent_Declare(
			zlib
			GIT_REPOSITORY "https://github.com/madler/zlib.git"
			GIT_TAG v1.3.1
			FIND_PACKAGE_ARGS NAMES ZLIB
		)
		FetchContent_MakeAvailable(zlib)
		set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR})
		set(ZLIB_LIBRARY zlibstatic CACHE STRING "")
		set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})
		set(ZLIB_FOUND 1)
		add_library(ZLIB::ZLIB ALIAS zlibstatic)
	else ()
		find_package(ZLIB REQUIRED)
		target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)
	endif()
else()
	find_package(spdlog QUIET)
    if(NOT spdlog_FOUND)
        FetchContent_Declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG 79524ddd08a4ec981b7fea76afd08ee05f83755d
        )

        FetchContent_MakeAvailable(spdlog)
    endif()
	target_link_libraries(${PROJECT_NAME} PUBLIC spdlog::spdlog)
endif()

# Link TinyXML2
set(tinyxml2_BUILD_TESTING OFF)
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 10.0.0
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(tinyxml2)

# Link zlib
if(EMSCRIPTEN OR ANDROID)
    find_package(ZLIB REQUIRED)
    set(TORCH_ZLIB_TARGET ZLIB::ZLIB)
else()
    set(ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL "Build zlib as a static library" FORCE)
    set(ZLIB_BUILD_TESTS OFF CACHE BOOL "Disable building zlib tests" FORCE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "Skip zlib install targets" FORCE)

    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(zlib)
    set(TORCH_ZLIB_TARGET zlibstatic)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE tinyxml2 ${TORCH_ZLIB_TARGET} yaml-cpp N64Graphics BinaryTools)

if(BUILD_UI)
    target_link_libraries(${PROJECT_NAME} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,libultraship>")
    set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS TRUE)
endif()

if(NOT USE_STANDALONE)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_include_directories(${PROJECT_NAME} PUBLIC ${yaml-cpp_SOURCE_DIR}/include)
endif()
