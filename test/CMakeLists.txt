# CMakeLists.txt for unit tests

cmake_minimum_required(VERSION 3.19)

# Find required packages
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(eigen3 CONFIG REQUIRED)
find_package(gtest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(entt CONFIG REQUIRED)

# MSVC specific settings
if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

# MinGW specific settings: support large object files
if(MINGW OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    add_compile_options(-Wa,-mbig-obj)
endif()

# Add compile definitions
add_compile_definitions(
    SPDLOG_HEADER_ONLY 
    FMT_HEADER_ONLY
    ENTT_ID_TYPE=std::uint64_t
)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/data_center)
include_directories(${CMAKE_SOURCE_DIR}/system)

# Collect source files needed for tests (exclude main.cpp)
file(GLOB_RECURSE SYSTEM_SOURCES
    "${CMAKE_SOURCE_DIR}/system/*.cpp"
    "${CMAKE_SOURCE_DIR}/system/*.c"
)
# Exclude main.cpp from test builds
list(REMOVE_ITEM SYSTEM_SOURCES "${CMAKE_SOURCE_DIR}/system/main.cpp")

# Test executable for assembly system
add_executable(test_assembly_system 
    test_assembly_system.cpp
    ${SYSTEM_SOURCES}
)

# Link libraries
target_link_libraries(test_assembly_system
    spdlog::spdlog
    fmt::fmt
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    EnTT::EnTT
    GTest::gtest
    GTest::gtest_main
)

# Enable testing
enable_testing()

# Add test
add_test(NAME test_assembly_system COMMAND test_assembly_system)

# Set output directory (match parent project structure)
if(MSVC)
    set_target_properties(test_assembly_system PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc/tests
    )
else()
    set_target_properties(test_assembly_system PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/tests
    )
endif()

