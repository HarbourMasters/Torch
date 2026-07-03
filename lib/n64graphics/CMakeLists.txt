cmake_minimum_required(VERSION 3.12)
project(N64Graphics)

set(CMAKE_CXX_STANDARD 20)

file(GLOB CXX_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
set(SRC_DIR ${CXX_FILES})

add_library(${PROJECT_NAME} STATIC ${SRC_DIR})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_UI)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        stbi_write_png=n64g_stbi_write_png
        stbi_write_bmp=n64g_stbi_write_bmp
        stbi_write_tga=n64g_stbi_write_tga
        stbi_write_hdr=n64g_stbi_write_hdr
        stbi_write_jpg=n64g_stbi_write_jpg
        stbi_write_png_to_func=n64g_stbi_write_png_to_func
        stbi_write_bmp_to_func=n64g_stbi_write_bmp_to_func
        stbi_write_tga_to_func=n64g_stbi_write_tga_to_func
        stbi_write_hdr_to_func=n64g_stbi_write_hdr_to_func
        stbi_write_jpg_to_func=n64g_stbi_write_jpg_to_func
        stbi_write_png_to_mem=n64g_stbi_write_png_to_mem
        stbi_flip_vertically_on_write=n64g_stbi_flip_vertically_on_write
        stbiw_convert_wchar_to_utf8=n64g_stbiw_convert_wchar_to_utf8
        stbi_zlib_compress=n64g_stbi_zlib_compress
        stbi_write_tga_with_rle=n64g_stbi_write_tga_with_rle
        stbi_write_png_compression_level=n64g_stbi_write_png_compression_level
        stbi_write_force_png_filter=n64g_stbi_write_force_png_filter
    )
endif()