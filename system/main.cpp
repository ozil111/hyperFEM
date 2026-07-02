/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include <spdlog/spdlog.h>
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "parser_base/parserBase.h"       // Legacy .xfem parser (backward compatible)
#include "parser_json/JsonParser.h"       // New JSON parser
#include "exporter_base/exporterBase.h"
#include "DataContext.h"                  // Introduce ECS data center
#include "components/mesh_components.h"   // Introduce component definitions
#include "components/analysis_component.h"
#include "AppSession.h"                   // Introduce session state machine
#include "output/VtuExporter.h"          // VTU result output
#include "main0_explicit.h"              // Explicit solver logic
#include "main0_linearstatic.h"          // Linear static solver logic
#include "tui/ComponentTUI.h"
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include "CommandProcessor.h"

// Function to print the startup banner
void print_banner() {
    // You can use an online ASCII art generator to create your own style
    std::cout << R"(
    _   __                 ______  ______      _    
   / | / /___ _   ______ _/ ____/ / ____/___ _/ |   
  /  |/ / __ \ | / / __ `/ /_    / __/ / __ `/ /    
 / /|  / /_/ / |/ / /_/ / __/   / /___/ /_/ / /     
/_/ |_/\____/|___/\__,_/_/    /_____/\__,_/_/      
)" << std::endl;
    std::cout << "  NovaFEA Version: 0.0.1" << std::endl;
    std::cout << "  Author: xiaotong wang" << std::endl;
    std::cout << "  Email:  xiaotongwang98@gmail.com" << std::endl;
    std::cout << "---------------------------------------------------------" << std::endl << std::endl;
}

// Function to print help information
void print_help() {
    std::cout << "Usage: NovaFEA_app [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --input-file, -i <file>    Specify input file (.xfem or .json/.jsonc)" << std::endl;
    std::cout << "  --export, -e <file>        Export preprocessed mesh (.xfem or .jsonc)" << std::endl;
    std::cout << "  --output, -o <file>        Output result file (.vtu)" << std::endl;
    std::cout << "  --output-file <file>       [deprecated] Alias for --export (.xfem or .jsonc)" << std::endl;
    std::cout << "  --log-level, -l <level>    Set log level (trace, debug, info, warn, error, critical)" << std::endl;
    std::cout << "  --log-directory, -d <path> Set log file path" << std::endl;
    std::cout << "  --script, -s <file>        Run commands from a script file (one per line, '#' for comments)" << std::endl;
    std::cout << "  --help, -h                 Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Supported Input Formats:" << std::endl;
    std::cout << "  .xfem  - Legacy text format (backward compatible)" << std::endl;
    std::cout << "  .json  - JSON format (recommended)" << std::endl;
    std::cout << "  .jsonc - JSON with comments (recommended)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  NovaFEA_app --input-file case/model.jsonc --export case/output.xfem" << std::endl;
    std::cout << "  NovaFEA_app --input-file case/model.jsonc --output case/result.vtu" << std::endl;
    std::cout << "  NovaFEA_app --input-file case/node.xfem --export case/output.xfem" << std::endl;
}

// --- Introduce interactive mode command processor ---

int main(int argc, char* argv[]) {
    // --- Step 1: Proceed with argument parsing and logger setup ---
    
    // Default log level is info
    spdlog::level::level_enum log_level = spdlog::level::info;
    
    // Default log file path
    std::string log_file_path = "logs/NovaFEA.log";
    
    // Input file path
    std::string input_file_path;
    
    // Export mesh file path (old --output-file behavior)
    std::string export_file_path;
    
    // Result output .vtu file path (new output)
    std::string output_vtu_path;

    // Script file path (for script mode)
    std::string script_file_path;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else if (arg == "--input-file" || arg == "-i") {
            if (i + 1 < argc) {
                input_file_path = argv[++i];
                
                // Validate file extension (supports .xfem, .json, .jsonc)
                std::filesystem::path file_path(input_file_path);
                std::string extension = file_path.extension().string();
                if (extension != ".xfem" && extension != ".json" && extension != ".jsonc") {
                    std::cerr << "Error: Input file must have .xfem, .json, or .jsonc extension" << std::endl;
                    std::cerr << "Provided file: " << input_file_path << std::endl;
                    return 1;
                }
                
                // Validate file existence
                if (!std::filesystem::exists(file_path)) {
                    std::cerr << "Error: Input file does not exist: " << input_file_path << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --input-file requires a file path argument" << std::endl;
                return 1;
            }
        } else if (arg == "--log-level" || arg == "-l") {
            if (i + 1 < argc) {
                std::string level_str = argv[++i];
                if (level_str == "trace") {
                    log_level = spdlog::level::trace;
                } else if (level_str == "debug") {
                    log_level = spdlog::level::debug;
                } else if (level_str == "info") {
                    log_level = spdlog::level::info;
                } else if (level_str == "warn" || level_str == "warning") {
                    log_level = spdlog::level::warn;
                } else if (level_str == "error") {
                    log_level = spdlog::level::err;
                } else if (level_str == "critical") {
                    log_level = spdlog::level::critical;
                } else {
                    std::cerr << "Unknown log level: " << level_str << std::endl;
                    std::cerr << "Valid levels: trace, debug, info, warn, error, critical" << std::endl;
                    return 1;
                }
            } 
        } else if (arg == "--export" || arg == "--output-file" || arg == "-e") {
            if (i + 1 < argc) {
                export_file_path = argv[++i];
                
                // Validate file extension (supports .xfem, .jsonc)
                std::filesystem::path file_path(export_file_path);
                auto ext = file_path.extension().string();
                if (ext != ".xfem" && ext != ".jsonc") {
                    std::cerr << "Error: Export file must have .xfem or .jsonc extension" << std::endl;
                    std::cerr << "Provided file: " << export_file_path << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --export/--output-file requires a file path argument" << std::endl;
                return 1;
            }
        } else if (arg == "--output" || arg == "-o") {
            if (i + 1 < argc) {
                output_vtu_path = argv[++i];

                // Validate file extension (only supports .vtu)
                std::filesystem::path file_path(output_vtu_path);
                if (file_path.extension() != ".vtu") {
                    std::cerr << "Error: Output file must have .vtu extension" << std::endl;
                    std::cerr << "Provided file: " << output_vtu_path << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --output requires a file path argument" << std::endl;
                return 1;
            }
        } else if (arg == "--log-directory" || arg == "-d") {
            if (i + 1 < argc) {
                log_file_path = argv[++i];
            } else {
                std::cerr << "Error: --log-directory requires a path argument" << std::endl;
                return 1;
            }
        } else if (arg == "--script" || arg == "-s") {
            if (i + 1 < argc) {
                script_file_path = argv[++i];
            } else {
                std::cerr << "Error: --script requires a file path argument" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            std::cerr << "Use --help or -h for usage information" << std::endl;
            return 1;
        }
    }
    
    // Create multiple sinks: file and console
    std::vector<spdlog::sink_ptr> sinks;
    
    // File output sink - output to user-specified log file
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path, true);
    sinks.push_back(file_sink);
    
    // Console output sink
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(console_sink);
    
    // Create logger and register
    auto logger = std::make_shared<spdlog::logger>("NovaFEA", begin(sinks), end(sinks));
    logger->set_level(log_level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
    
    // --- Step 3: Now use the logger for actual logging ---
    spdlog::info("Logger initialized. Application starting...");
    spdlog::info("Log level set to: {}", spdlog::level::to_string_view(log_level));
    
    // --- Step 4: Mode decision ---
    // Decide which mode to enter: script mode > batch mode > interactive mode
    if (!script_file_path.empty()) {
        // --- SCRIPT MODE EXECUTION ---
        spdlog::info("Running in Script Mode.");
        spdlog::info("Executing script file: {}", script_file_path);

        AppSession session;
        std::ifstream script_file(script_file_path);
        if (!script_file.is_open()) {
            spdlog::error("Cannot open script file: {}", script_file_path);
            return 1;
        }

        std::string line;
        int line_num = 0;
        while (session.is_running && std::getline(script_file, line)) {
            ++line_num;
            // Skip blank lines and comments (lines whose first non-whitespace char is '#')
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            if (line[first] == '#') continue;

            spdlog::info("[script:{}]> {}", line_num, line);
            process_command(line, session);
        }
        spdlog::info("Script execution finished. {} lines processed.", line_num);
    } else if (!input_file_path.empty()) {
        // Banner is meaningful in batch mode output.
        print_banner();
        // --- BATCH MODE EXECUTION ---
        spdlog::info("Running in Batch Mode.");
        spdlog::info("Processing input file: {}", input_file_path);
        
        // Create DataContext object to store parsed data
        DataContext data_context;
        // Record command line specified VTU output path (if any), used to suppress default result/*.vtu output in solver
        data_context.cli_output_vtu_path = output_vtu_path;
        
        // Automatically select parser based on file extension
        std::filesystem::path path(input_file_path);
        std::string extension = path.extension().string();
        bool parse_success = false;
        
        if (extension == ".json" || extension == ".jsonc") {
            spdlog::info("Detected JSON format, using JsonParser...");
            parse_success = JsonParser::parse(input_file_path, data_context);
        } else if (extension == ".xfem") {
            spdlog::info("Detected XFEM format, using FemParser (legacy)...");
            parse_success = FemParser::parse(input_file_path, data_context);
        }
        
        if (parse_success) {
            spdlog::info("Successfully parsed input file: {}", input_file_path);
            
            // Count entities using views
            auto node_count = data_context.registry.view<Component::Position>().size();
            auto element_count = data_context.registry.view<Component::Connectivity>().size();
            auto set_count = data_context.registry.view<Component::SetName>().size();
            
            spdlog::info("Total nodes loaded: {}", node_count);
            spdlog::info("Total elements loaded: {}", element_count);
            spdlog::info("Total sets loaded: {}", set_count);
            
            // --- Step 5: Run solver if analysis type is specified ---
            if (data_context.analysis_entity != entt::null
                && data_context.registry.valid(data_context.analysis_entity)
                && data_context.registry.all_of<Component::AnalysisType>(data_context.analysis_entity)
                && data_context.registry.get<Component::AnalysisType>(data_context.analysis_entity).value == "explicit") {
                run_explicit_solver(data_context);
            } else if (data_context.analysis_entity != entt::null
                && data_context.registry.valid(data_context.analysis_entity)
                && data_context.registry.all_of<Component::AnalysisType>(data_context.analysis_entity)
                && data_context.registry.get<Component::AnalysisType>(data_context.analysis_entity).value == "static") {
                run_linearstatic_solver(data_context);
            }
            
            // --- Step 6: Export the mesh if an export file is specified ---
            if (!export_file_path.empty()) {
                spdlog::info("Exporting mesh data to: {}", export_file_path);
                if (FemExporter::save(export_file_path, data_context)) {
                    spdlog::info("Successfully exported mesh data.");
                } else {
                    spdlog::error("Failed to export mesh data to: {}", export_file_path);
                    return 1;
                }
            }

            // --- Step 7: Export VTU result if an output file is specified ---
            if (!output_vtu_path.empty()) {
                spdlog::info("Exporting VTU result to: {}", output_vtu_path);
                if (VtuExporter::save(output_vtu_path, data_context, data_context.output_entity)) {
                    spdlog::info("Successfully exported VTU result.");
                } else {
                    spdlog::error("Failed to export VTU result to: {}", output_vtu_path);
                    return 1;
                }
            }
            
        } else {
            spdlog::error("Failed to parse input file: {}", input_file_path);
            return 1;
        }
    } else {
        // In interactive mode we start the FTXUI TUI; banner is shown inside the TUI.
        // --- INTERACTIVE MODE EXECUTION ---
        tui::install_tui_log_sink();
        spdlog::info("No input file specified. Running in Interactive Mode.");
        
        AppSession session;
        tui::run_app_tui(session);
    }
    
    spdlog::info("Application finished successfully.");
    return 0;
}