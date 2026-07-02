# CMakeLists.txt for unit tests

cmake_minimum_required(VERSION 3.19)

# Find required packages
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(eigen3 CONFIG REQUIRED)
find_package(gtest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(entt CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)

# MSVC specific settings
if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

# MinGW ABI on Windows (GCC or Clang): support for large object files
if(WIN32 AND NOT MSVC)
    add_compile_options(-Wa,-mbig-obj)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wno-invalid-consteval)
endif()

add_compile_definitions(
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

# Test executable for JSON exporter
add_executable(test_json_exporter 
    test_json_exporter.cpp
    ${SYSTEM_SOURCES}
)

add_executable(test_connection_preserving_remesher
    test_connection_preserving_remesher.cpp
    ${SYSTEM_SOURCES}
)

# Test executable for Abaqus parser
add_executable(test_abaqus_parser
    test_abaqus_parser.cpp
    ${SYSTEM_SOURCES}
)
target_compile_definitions(test_abaqus_parser PRIVATE
    TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/test/cases"
)

# Link libraries (match parent: header-only spdlog/fmt only)
target_link_libraries(test_assembly_system PRIVATE
    fmt::fmt-header-only
    spdlog::spdlog_header_only
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    EnTT::EnTT
    ftxui::dom
    ftxui::component
    ftxui::screen
    tinyxml2::tinyxml2
    GTest::gtest
    GTest::gtest_main
)

target_link_libraries(test_json_exporter PRIVATE
    fmt::fmt-header-only
    spdlog::spdlog_header_only
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    EnTT::EnTT
    ftxui::dom
    ftxui::component
    ftxui::screen
    tinyxml2::tinyxml2
    GTest::gtest
    GTest::gtest_main
)

target_link_libraries(test_connection_preserving_remesher PRIVATE
    fmt::fmt-header-only
    spdlog::spdlog_header_only
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    EnTT::EnTT
    ftxui::dom
    ftxui::component
    ftxui::screen
    tinyxml2::tinyxml2
    GTest::gtest
    GTest::gtest_main
)

target_link_libraries(test_abaqus_parser PRIVATE
    fmt::fmt-header-only
    spdlog::spdlog_header_only
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    EnTT::EnTT
    ftxui::dom
    ftxui::component
    ftxui::screen
    tinyxml2::tinyxml2
    GTest::gtest
    GTest::gtest_main
)

# Enable testing
enable_testing()

# Add tests
add_test(NAME test_assembly_system COMMAND test_assembly_system)
add_test(NAME test_json_exporter COMMAND test_json_exporter)
add_test(NAME test_connection_preserving_remesher COMMAND test_connection_preserving_remesher)
add_test(NAME test_abaqus_parser COMMAND test_abaqus_parser)
# Set output directory (match parent project structure)
if(MSVC)
    set_target_properties(test_assembly_system test_json_exporter test_connection_preserving_remesher test_abaqus_parser PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/msvc/tests
    )
else()
    set_target_properties(test_assembly_system test_json_exporter test_connection_preserving_remesher test_abaqus_parser PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/tests
    )
endif()

if(WIN32 AND NOT MSVC)
    find_program(MINGW_GXX g++ REQUIRED)
    get_filename_component(MINGW_BIN_DIR ${MINGW_GXX} DIRECTORY)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(VCPKG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed/${NOVAFEA_VCPKG_TRIPLET}/debug/bin")
    else()
        set(VCPKG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed/${NOVAFEA_VCPKG_TRIPLET}/bin")
    endif()

    foreach(TEST_TARGET test_assembly_system test_json_exporter test_connection_preserving_remesher test_abaqus_parser)
        add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                "$<TARGET_FILE_DIR:${TEST_TARGET}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libstdc++-6.dll"
                "$<TARGET_FILE_DIR:${TEST_TARGET}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                "$<TARGET_FILE_DIR:${TEST_TARGET}>"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VCPKG_BIN_DIR}"
                "$<TARGET_FILE_DIR:${TEST_TARGET}>"
            COMMENT "Copying runtime DLLs for ${TEST_TARGET}"
        )
    endforeach()
endif()

