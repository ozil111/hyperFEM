// test_abaqus_exporter.cpp
// Unit + integration tests for the Abaqus .inp exporter.
//
// Test strategy (per requirements):
//   1. Parse the reference T01.inp into DataContext (round-trip source)
//   2. Export the DataContext to a new .inp file via AbaqusExporter
//   3. Invoke `abaqus job=... int` on the exported file
//   4. Verify the job completed successfully (check .com file)
//   5. Clean up ALL generated artifacts (.com, .dat, .odb, etc.)
//
// If `abaqus` is not on PATH, the integration portion is skipped (GTEST_SKIP)
// but the export / round-trip checks still run.

#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <thread>
#include <algorithm>

#include "DataContext.h"
#include "parser_abaqus/AbaqusParser.h"
#include "exporter_abaqus/AbaqusExporter.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "components/load_components.h"
#include "components/analysis_component.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_valid(entt::entity e) { return e != entt::null; }

/// Check whether the `abaqus` command is available on PATH.
static bool abaqus_available() {
#ifdef _WIN32
    int rc = std::system("where abaqus >nul 2>nul");
#else
    int rc = std::system("command -v abaqus >/dev/null 2>&1");
#endif
    return rc == 0;
}

/// Run `abaqus job=<name> int ask=off` in the given directory.
/// Returns the command exit code.
/// Uses a temporary .bat file to ensure correct cd behavior on Windows.
static int run_abaqus(const std::string& job_name, const std::string& work_dir) {
    // Build a temp batch file for reliable cd + command execution
    fs::path bat_path = fs::path(work_dir) / "_run_abaqus_tmp.bat";
    std::ofstream bat(bat_path);
    if (!bat.is_open()) return -1;
#ifdef _WIN32
    bat << "@echo off\n";
    bat << "cd /d \"" << work_dir << "\"\n";
    bat << "abaqus job=" << job_name << " int ask=off\n";
#else
    bat << "#!/bin/sh\n";
    bat << "cd \"" << work_dir << "\"\n";
    bat << "abaqus job=" << job_name << " int ask=off\n";
#endif
    bat.close();

    std::string cmd;
#ifdef _WIN32
    cmd = "cmd /c \"" + bat_path.string() + "\" >nul 2>nul";
#else
    cmd = "sh \"" + bat_path.string() + "\" >/dev/null 2>&1";
#endif
    int rc = std::system(cmd.c_str());

    std::error_code ec;
    fs::remove(bat_path, ec);
    return rc;
}

/// Read a text file into a string; returns empty string on failure.
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/// Clean up all Abaqus-generated artifacts for a given job name in a directory.
/// Extension list aligned with extern/vuel/run_abaqus.py.
static void cleanup_abaqus_artifacts(const std::string& work_dir, const std::string& job_name) {
    static const std::vector<std::string> exts = {
        ".abq", ".com", ".dat", ".mdl", ".msg", ".pac", ".prt",
        ".res", ".sel", ".sta", ".stt", ".log", ".rpy", ".rpyc",
        ".exception", ".SMABulk", ".SimConDB", ".sim",
        ".input", ".ipm", ".oss", ".cft", ".isd", ".rsf", ".pmn", ".rep",
        ".odb"
    };
    fs::path base(work_dir);
    std::string job_lower = job_name;
    std::transform(job_lower.begin(), job_lower.end(), job_lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    for (const auto& f : fs::directory_iterator(base)) {
        if (!f.is_regular_file()) continue;
        std::string ext = f.path().extension().string();
        // Keep .inp always (we remove it separately if needed)
        if (ext == ".inp") continue;
        // Match job name (case-insensitive) or job_X1/job_X2 partition files
        std::string stem = f.path().stem().string();
        std::string stem_lower = stem;
        std::transform(stem_lower.begin(), stem_lower.end(), stem_lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        bool is_job_file = (stem_lower == job_lower);
        if (!is_job_file) {
            std::string prefix = job_lower + "_x";
            if (stem_lower.rfind(prefix, 0) == 0) {
                std::string rest = stem_lower.substr(prefix.size());
                is_job_file = !rest.empty() &&
                              std::all_of(rest.begin(), rest.end(),
                                          [](unsigned char c){ return std::isdigit(c); });
            }
        }
        if (!is_job_file) continue;

        // Check if extension is in our cleanup list (case-insensitive)
        std::string ext_lower = ext;
        std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        bool should_remove = false;
        for (const auto& e : exts) {
            if (ext_lower == e) { should_remove = true; break; }
        }
        if (should_remove) {
            std::error_code ec;
            fs::remove(f.path(), ec);
        }
    }
    // Remove the exported .inp
    {
        std::error_code ec;
        fs::remove(base / (job_name + ".inp"), ec);
    }
}

// ---------------------------------------------------------------------------
// Fixture: parse reference T01.inp once
// ---------------------------------------------------------------------------
class AbaqusExporterTest : public ::testing::Test {
protected:
    DataContext ctx;
    std::string source_inp;
    std::string work_dir;
    std::string exported_inp;
    std::string job_name;

    void SetUp() override {
        source_inp = std::string(TEST_DATA_DIR) + "/abaqus_T01.inp";
        ASSERT_TRUE(AbaqusParser::parse(source_inp, ctx))
            << "Failed to parse source: " << source_inp;

        // Use the test/cases directory as the working directory so abaqus
        // artifacts are co-located and easy to clean up.
        work_dir = std::string(TEST_DATA_DIR);
        job_name = "exported_T01_test";
        exported_inp = work_dir + "/" + job_name + ".inp";

        // Make sure no stale artifacts from a previous run
        cleanup_abaqus_artifacts(work_dir, job_name);
    }

    void TearDown() override {
        // Always clean up, even on failure.
        // Set NOVAFEA_KEEP_ARTIFACTS=1 to retain files for debugging.
        const char* keep = std::getenv("NOVAFEA_KEEP_ARTIFACTS");
        if (keep && std::string(keep) == "1") {
            // Keep files for debugging
        } else {
            cleanup_abaqus_artifacts(work_dir, job_name);
        }
        ctx.clear();
    }
};

// ---------------------------------------------------------------------------
// Test 1: Export produces a non-empty file
// ---------------------------------------------------------------------------
TEST_F(AbaqusExporterTest, ExportProducesNonEmptyFile) {
    bool ok = AbaqusExporter::save(exported_inp, ctx);
    ASSERT_TRUE(ok) << "Exporter failed to save";

    ASSERT_TRUE(fs::exists(exported_inp)) << "Exported file does not exist";
    auto size = fs::file_size(exported_inp);
    EXPECT_GT(size, 0u) << "Exported file is empty";
}

// ---------------------------------------------------------------------------
// Test 2: Exported file contains all required Abaqus keywords
// ---------------------------------------------------------------------------
TEST_F(AbaqusExporterTest, ExportedFileHasRequiredKeywords) {
    ASSERT_TRUE(AbaqusExporter::save(exported_inp, ctx));

    std::string content = read_file(exported_inp);
    ASSERT_FALSE(content.empty());

    // Mandatory keywords for a runnable Abaqus/Explicit deck
    EXPECT_NE(content.find("*NODE"), std::string::npos);
    EXPECT_NE(content.find("*ELEMENT"), std::string::npos);
    EXPECT_NE(content.find("*SOLID SECTION"), std::string::npos);
    EXPECT_NE(content.find("*MATERIAL"), std::string::npos);
    EXPECT_NE(content.find("*DENSITY"), std::string::npos);
    EXPECT_NE(content.find("*ELASTIC"), std::string::npos);
    EXPECT_NE(content.find("*STEP"), std::string::npos);
    EXPECT_NE(content.find("*DYNAMIC"), std::string::npos);
    EXPECT_NE(content.find("*END STEP"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 3: Round-trip — re-parse the exported file and verify key data
// ---------------------------------------------------------------------------
TEST_F(AbaqusExporterTest, RoundTripDataConsistency) {
    ASSERT_TRUE(AbaqusExporter::save(exported_inp, ctx));

    DataContext reparsed;
    ASSERT_TRUE(AbaqusParser::parse(exported_inp, reparsed));

    // Node count should match
    auto orig_nodes = ctx.registry.view<const Component::Position>().size();
    auto new_nodes  = reparsed.registry.view<const Component::Position>().size();
    EXPECT_EQ(new_nodes, orig_nodes);

    // Element count should match
    auto orig_elems = ctx.registry.view<const Component::Connectivity>().size();
    auto new_elems  = reparsed.registry.view<const Component::Connectivity>().size();
    EXPECT_EQ(new_elems, orig_elems);

    // Material count should match
    auto orig_mats = ctx.registry.view<const Component::MaterialID>().size();
    auto new_mats  = reparsed.registry.view<const Component::MaterialID>().size();
    EXPECT_EQ(new_mats, orig_mats);

    // Material properties preserved
    {
        auto view = reparsed.registry.view<const Component::LinearElasticParams>();
        ASSERT_EQ(view.size(), 1u);
        auto e = view.front();
        const auto& p = reparsed.registry.get<Component::LinearElasticParams>(e);
        EXPECT_DOUBLE_EQ(p.rho, 1000.0);
        EXPECT_DOUBLE_EQ(p.E, 21000000.0);
        EXPECT_DOUBLE_EQ(p.nu, 0.3);
    }

    reparsed.clear();
}

// ---------------------------------------------------------------------------
// Test 4 (Integration): Run the exported file through actual Abaqus
// ---------------------------------------------------------------------------
TEST_F(AbaqusExporterTest, AbaqusCanRunExportedFile) {
    // Export first
    ASSERT_TRUE(AbaqusExporter::save(exported_inp, ctx));
    ASSERT_TRUE(fs::exists(exported_inp));

    if (!abaqus_available()) {
        GTEST_SKIP() << "abaqus command not available — skipping integration check";
    }

    // Run abaqus job
    int rc = run_abaqus(job_name, work_dir);

    // The .sta (status) file is the authoritative completion log.
    // Abaqus writes "THE ANALYSIS HAS COMPLETED SUCCESSFULLY" on success.
    std::string sta_path = work_dir + "/" + job_name + ".sta";
    std::string sta_content = read_file(sta_path);

    bool completed = sta_content.find("THE ANALYSIS HAS COMPLETED SUCCESSFULLY")
                     != std::string::npos;

    // Allow a brief moment for file flush
    if (!completed) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sta_content = read_file(sta_path);
        completed = sta_content.find("THE ANALYSIS HAS COMPLETED SUCCESSFULLY")
                    != std::string::npos;
    }

    // On failure, read .dat/.msg for error details to help debugging
    if (!completed) {
        std::string dat_path = work_dir + "/" + job_name + ".dat";
        std::string dat_content = read_file(dat_path);
        std::string msg_path = work_dir + "/" + job_name + ".msg";
        std::string msg_content = read_file(msg_path);

        FAIL() << "Abaqus job did not complete successfully.\n"
               << "  exit code: " << rc << "\n"
               << "  .sta file content:\n" << sta_content << "\n"
               << "  .dat file (first 2000 chars):\n"
               << dat_content.substr(0, 2000) << "\n"
               << "  .msg file (first 1000 chars):\n"
               << msg_content.substr(0, 1000);
    }
}
