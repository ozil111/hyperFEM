cmake_minimum_required(VERSION 3.19)

# Set CMake policy
cmake_policy(SET CMP0167 NEW)

# Disable automatic vcpkg install
set(VCPKG_MANIFEST_INSTALL OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set vcpkg toolchain file path
set(CMAKE_TOOLCHAIN_FILE "D:/Document/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# Set vcpkg installation path - dynamically based on triplet
if(DEFINED VCPKG_TARGET_TRIPLET)
    set(NOVAFEA_VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
else()
    set(NOVAFEA_VCPKG_TRIPLET "x64-mingw-dynamic")
endif()
set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/vcpkg_installed/${NOVAFEA_VCPKG_TRIPLET}" "${CMAKE_PREFIX_PATH}")

# Project name and C++ standard
project(NovaFEA)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories
if(MSVC)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc)
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
endif()

# MSVC specific settings
if(MSVC)
    add_compile_options(/utf-8 /bigobj)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

# MinGW ABI on Windows (GCC or Clang): support for large object files (resolves 'too many sections')
# This resolves compilation errors caused by large header libraries like nlohmann/json.hpp
if(WIN32 AND NOT MSVC)
    add_compile_options(-Wa,-mbig-obj)
endif()

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Include header directories
include_directories(${CMAKE_SOURCE_DIR}/data_center)
include_directories(${CMAKE_SOURCE_DIR}/system)

# Find dependencies
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(eigen3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(entt CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)

# Header-only spdlog + external fmt (no spdlog/fmt shared library usage)
add_compile_definitions(FMT_HEADER_ONLY)
add_compile_definitions(SPDLOG_HEADER_ONLY)
add_compile_definitions(SPDLOG_FMT_EXTERNAL)

# Clang + libstdc++ (e.g. clang++ --target=x86_64-w64-windows-gnu): fmt's consteval format
# string checker is not a constant expression; empty FMT_CONSTEVAL skips FMT_HAS_CONSTEVAL.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_definitions("FMT_CONSTEVAL=")
endif()

# 64-bit entity IDs to support large-scale meshes (default 32-bit caps at ~1M entities)
add_compile_definitions(ENTT_ID_TYPE=std::uint64_t)

# Collect source files
file(GLOB_RECURSE NovaFEA_SOURCES
    system/*.cpp
    system/*.c
)

# Exclude convenience implementation file, as its content has been separated into individual modules
list(REMOVE_ITEM NovaFEA_SOURCES "${CMAKE_SOURCE_DIR}/src/NovaFEA_implementations.cpp")

# Main executable
add_executable(NovaFEA_app ${NovaFEA_SOURCES})

# Link dependencies (spdlog/fmt: header-only targets only — never mix with compiled ::spdlog / ::fmt)
target_link_libraries(NovaFEA_app PRIVATE
    fmt::fmt-header-only
    spdlog::spdlog_header_only
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    EnTT::EnTT
    tinyxml2::tinyxml2
    ftxui::dom
    ftxui::component
    ftxui::screen
)

# Set working directory for main program
set_target_properties(NovaFEA_app PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ========== Automatically copy dependent DLLs to output directory ==========
if(WIN32)
    if(MSVC)
        # MSVC: Copy vcpkg DLLs
        add_custom_command(TARGET NovaFEA_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying MSVC DLLs..."
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/$<IF:$<CONFIG:Debug>,debug/,>bin/*.dll"
                "$<TARGET_FILE_DIR:NovaFEA_app>"
            COMMENT "Copying runtime DLLs for MSVC build"
        )
    elseif(WIN32 AND NOT MSVC)
        # MinGW ABI: Copy vcpkg DLLs and MinGW runtime DLLs (Clang driver lives in LLVM bin, not MinGW)
        find_program(MINGW_GXX g++ REQUIRED)
        get_filename_component(MINGW_BIN_DIR ${MINGW_GXX} DIRECTORY)
        
        # vcpkg shared libs for this triplet (must match NOVAFEA_VCPKG_TRIPLET / CMAKE_PREFIX_PATH)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(VCPKG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed/${NOVAFEA_VCPKG_TRIPLET}/debug/bin")
        else()
            set(VCPKG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed/${NOVAFEA_VCPKG_TRIPLET}/bin")
        endif()
        set(TINYXML2_DLL "libtinyxml2.dll")
        
        add_custom_command(TARGET NovaFEA_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying MinGW DLLs for ${CMAKE_BUILD_TYPE} build..."
            
            # Copy vcpkg DLLs (spdlog/fmt are header-only; tinyxml2 may still be shared)
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                "$<TARGET_FILE_DIR:NovaFEA_app>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libstdc++-6.dll"
                "$<TARGET_FILE_DIR:NovaFEA_app>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                "$<TARGET_FILE_DIR:NovaFEA_app>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VCPKG_BIN_DIR}/${TINYXML2_DLL}"
                "$<TARGET_FILE_DIR:NovaFEA_app>"
            COMMENT "Copying runtime DLLs for MinGW build"
        )
    endif()
endif()