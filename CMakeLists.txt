cmake_minimum_required(VERSION 3.19)

# 设置 CMake 策略
cmake_policy(SET CMP0167 NEW)

# 禁用自动运行 vcpkg install
set(VCPKG_MANIFEST_INSTALL OFF)

# 设置 vcpkg 工具链文件路径
set(CMAKE_TOOLCHAIN_FILE "D:/Document/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# 设置 vcpkg 安装路径 - 根据 triplet 动态设置
if(DEFINED VCPKG_TARGET_TRIPLET)
    set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}" "${CMAKE_PREFIX_PATH}")
else()
    # 默认使用 x64-mingw-dynamic
    set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-mingw-dynamic" "${CMAKE_PREFIX_PATH}")
endif()

# 项目名称和 C++ 标准
project(hyperFEM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录
if(MSVC)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc)
    set(TEST_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc/tests)
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
    set(TEST_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/tests)
endif()

# MSVC 特定设置
if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

# MinGW 特定设置：支持大目标文件（解决 "too many sections" 错误）
# 当使用 MinGW 编译器时，添加 -Wa,-mbig-obj 标志以支持大目标文件
# 这解决了 nlohmann/json.hpp 等大型头文件库导致的编译错误
if(MINGW OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    add_compile_options(-Wa,-mbig-obj)
endif()

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/data_center)
include_directories(${CMAKE_SOURCE_DIR}/system)

# 查找依赖库
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(eigen3 CONFIG REQUIRED)
find_package(gtest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(entt CONFIG REQUIRED)

# 添加编译定义以使用header-only模式
add_compile_definitions(
    SPDLOG_HEADER_ONLY 
    FMT_HEADER_ONLY
)

# 配置EnTT使用64位实体ID以支持大规模网格
# 默认32位ID最多支持约100万个实体（2^20 = 1,048,576）
# 64位ID支持几乎无限数量的实体，适合大型FEM模型
add_compile_definitions(
    ENTT_ID_TYPE=std::uint64_t
)

# 收集源文件
file(GLOB_RECURSE hyperFEM_SOURCES
    system/*.cpp
    system/*.c
)

# 排除便利性实现文件，因为它的内容已经被分离到各个模块中
list(REMOVE_ITEM hyperFEM_SOURCES "${CMAKE_SOURCE_DIR}/src/hyperFEM_implementations.cpp")

# 主可执行文件
add_executable(hyperFEM_app ${hyperFEM_SOURCES})

# 链接依赖库
target_link_libraries(hyperFEM_app 
    spdlog::spdlog
    fmt::fmt
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    EnTT::EnTT
)

# 设置主程序的工作目录
set_target_properties(hyperFEM_app PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ========== 自动复制依赖DLL到输出目录 ==========
if(WIN32)
    if(MSVC)
        # MSVC: 复制vcpkg的DLL
        add_custom_command(TARGET hyperFEM_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying MSVC DLLs..."
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/$<IF:$<CONFIG:Debug>,debug/,>bin/*.dll"
                "$<TARGET_FILE_DIR:hyperFEM_app>"
            COMMENT "Copying runtime DLLs for MSVC build"
        )
    else()
        # MinGW: 复制vcpkg DLL和MinGW运行时DLL
        # 查找MinGW的安装路径
        get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        
        # 根据构建类型设置vcpkg DLL目录
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(VCPKG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-mingw-dynamic/debug/bin")
            set(SPDLOG_DLL "libspdlogd.dll")
            set(FMT_DLL "libfmtd.dll")
        else()
            set(VCPKG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-mingw-dynamic/bin")
            set(SPDLOG_DLL "libspdlog.dll")
            set(FMT_DLL "libfmt.dll")
        endif()
        
        add_custom_command(TARGET hyperFEM_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying MinGW DLLs for ${CMAKE_BUILD_TYPE} build..."
            
            # 复制vcpkg的DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VCPKG_BIN_DIR}/${SPDLOG_DLL}"
                "$<TARGET_FILE_DIR:hyperFEM_app>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VCPKG_BIN_DIR}/${FMT_DLL}"
                "$<TARGET_FILE_DIR:hyperFEM_app>"
            
            # 复制MinGW运行时DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                "$<TARGET_FILE_DIR:hyperFEM_app>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libstdc++-6.dll"
                "$<TARGET_FILE_DIR:hyperFEM_app>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                "$<TARGET_FILE_DIR:hyperFEM_app>"
            
            COMMENT "Copying runtime DLLs for MinGW build"
        )
    endif()
endif()

# 包含测试模块
option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()