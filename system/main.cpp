#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "parser_base/parserBase.h"
#include "exporter_base/exporterBase.h"
#include "mesh.h"
#include "TopologyData.h"      // 引入拓扑数据结构
#include "mesh/TopologySystems.h"   // 引入拓扑逻辑系统
#include "AppSession.h"        // 引入会话状态机
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <filesystem>
#include <sstream>

// Function to print the startup banner
void print_banner() {
    // You can use an online ASCII art generator to create your own style
    std::cout << R"(
    .__                              ______________________   _____   
    |  |__ ___.__.______   __________\_   _____/\_   _____/  /     \
    |  |  <   |  |\____ \_/ __ \_  __ \    __)   |    __)_  /  \ /  \
    |   Y  \___  ||  |_> >  ___/|  | \/     \    |        \/    Y    \
    |___|  / ____||   __/ \___  >__|  \___  /   /_______  /\____|__  /
        \/\/     |__|        \/          \/            \/         \/ 

)" << std::endl;
    std::cout << "  hyperFEM Version: 0.0.1" << std::endl;
    std::cout << "  Author: xiaotong wang" << std::endl;
    std::cout << "  Email:  xiaotongwang98@gmail.com" << std::endl;
    std::cout << "---------------------------------------------------------" << std::endl << std::endl;
}

// Function to print help information
void print_help() {
    std::cout << "Usage: hyperFEM_app [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --input-file, -i <file>    Specify input .xfem file to process" << std::endl;
    std::cout << "  --output-file, -o <file>   Specify output .xfem file to save" << std::endl;
    std::cout << "  --log-level, -l <level>    Set log level (trace, debug, info, warn, error, critical)" << std::endl;
    std::cout << "  --log-directory, -d <path> Set log file path" << std::endl;
    std::cout << "  --help, -h                 Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  hyperFEM_app --input-file case/node.xfem --output-file case/output.xfem" << std::endl;
}

// --- 引入交互模式的命令处理器 ---
void process_command(const std::string& command_line, AppSession& session) {
    std::stringstream ss(command_line);
    std::string command;
    ss >> command;

    if (command == "quit" || command == "exit") {
        session.is_running = false;
        spdlog::info("Exiting hyperFEM. Goodbye!");
    }
    else if (command == "help") {
        spdlog::info("Available commands: import, info, build_topology, list_bodies, save, help, quit");
    }
    else if (command == "import") {
        std::string file_path;
        ss >> file_path;
        if (file_path.empty()) {
            spdlog::error("Usage: import <path_to_xfem_file>");
            return;
        }
        session.clear_mesh();
        spdlog::info("Importing mesh from: {}", file_path);
        if (FemParser::parse(file_path, session.mesh)) {
            session.mesh_loaded = true;
            spdlog::info("Successfully imported mesh. {} nodes, {} elements.",
                         session.mesh.getNodeCount(), session.mesh.getElementCount());
        } else {
            spdlog::error("Failed to import mesh from: {}", file_path);
        }
    }
    else if (command == "build_topology") {
        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded. Please 'import' a mesh first.");
            return;
        }
        spdlog::info("Building topology data...");
        session.topology = std::make_unique<TopologyData>(session.mesh);
        TopologySystems::extract_topology(session.mesh, *session.topology);
        session.topology_built = true;
        spdlog::info("Topology built successfully. Found {} unique faces.", session.topology->faces.size());
    }
    else if (command == "list_bodies") {
        if (!session.topology_built) {
            spdlog::error("Topology not built. Please run 'build_topology' first.");
            return;
        }
        spdlog::info("Finding continuous bodies...");
        TopologySystems::find_continuous_bodies(*session.topology);
        spdlog::info("Found {} continuous body/bodies:", session.topology->body_to_elements.size());
        for (const auto& pair : session.topology->body_to_elements) {
            spdlog::info("  - Body {}: {} elements", pair.first, pair.second.size());
        }
    }
    else if (command == "show_body") {
        if (!session.topology_built) {
            spdlog::error("Topology not built. Please run 'build_topology' first.");
            return;
        }

        int body_id_to_show;
        if (!(ss >> body_id_to_show)) {
            spdlog::error("Usage: show_body <body_id>");
            return;
        }

        // Check if the requested BodyID exists
        const auto& body_map = session.topology->body_to_elements;
        auto it = body_map.find(body_id_to_show);

        if (it == body_map.end()) {
            spdlog::error("Body with ID {} not found. Use 'list_bodies' to see available bodies.", body_id_to_show);
            return;
        }

        const std::vector<ElementIndex>& element_indices = it->second;
        
        // Use a stringstream to build the output list to avoid printing too many lines
        std::stringstream element_list_ss;
        for (size_t i = 0; i < element_indices.size(); ++i) {
            ElementIndex elem_idx = element_indices[i];
            // Convert internal index back to external ID for user display
            ElementID elem_id = session.mesh.element_index_to_id[elem_idx];
            element_list_ss << elem_id << (i == element_indices.size() - 1 ? "" : ", ");
        }

        spdlog::info("Elements in Body {}:", body_id_to_show);
        spdlog::info("{}", element_list_ss.str());
    }
    else if (command == "save") {
        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded to save. Please 'import' a mesh first.");
            return;
        }
        std::string file_path;
        ss >> file_path;
        if (file_path.empty()) {
            spdlog::error("Usage: save <path_to_output_file.xfem>");
            return;
        }
        spdlog::info("Exporting mesh data to: {}", file_path);
        if (FemExporter::save(file_path, session.mesh)) {
            spdlog::info("Successfully exported mesh data.");
        } else {
            spdlog::error("Failed to export mesh data to: {}", file_path);
        }
    }
    else if (command == "info") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded.");
        } else {
            spdlog::info("Mesh loaded: {} nodes, {} elements, {} sets",
                         session.mesh.getNodeCount(), 
                         session.mesh.getElementCount(),
                         session.mesh.set_id_to_name.size());
            if (session.topology_built) {
                spdlog::info("Topology built: {} unique faces, {} bodies",
                             session.topology->faces.size(),
                             session.topology->body_to_elements.size());
            } else {
                spdlog::info("Topology not built yet.");
            }
        }
    }
    else {
        spdlog::warn("Unknown command: '{}'. Type 'help' for a list of commands.", command);
    }
}

int main(int argc, char* argv[]) {
    // --- Step 1: Print the banner first ---
    print_banner();

    // --- Step 2: Proceed with your original argument parsing and logger setup ---
    
    // 默认日志级别为info
    spdlog::level::level_enum log_level = spdlog::level::info;
    
    // 默认日志文件路径
    std::string log_file_path = "logs/hyperFEM.log";
    
    // 输入文件路径
    std::string input_file_path;
    
    // 输出文件路径
    std::string output_file_path;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else if (arg == "--input-file" || arg == "-i") {
            if (i + 1 < argc) {
                input_file_path = argv[++i];
                
                // 验证文件扩展名
                std::filesystem::path file_path(input_file_path);
                if (file_path.extension() != ".xfem") {
                    std::cerr << "Error: Input file must have .xfem extension" << std::endl;
                    std::cerr << "Provided file: " << input_file_path << std::endl;
                    return 1;
                }
                
                // 验证文件是否存在
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
        } else if (arg == "--output-file" || arg == "-o") {
            if (i + 1 < argc) {
                output_file_path = argv[++i];
                
                // 验证文件扩展名
                std::filesystem::path file_path(output_file_path);
                if (file_path.extension() != ".xfem") {
                    std::cerr << "Error: Output file must have .xfem extension" << std::endl;
                    std::cerr << "Provided file: " << output_file_path << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --output-file requires a file path argument" << std::endl;
                return 1;
            }
        } else if (arg == "--log-directory" || arg == "-d") {
            if (i + 1 < argc) {
                log_file_path = argv[++i];
            } else {
                std::cerr << "Error: --log-directory requires a path argument" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            std::cerr << "Use --help or -h for usage information" << std::endl;
            return 1;
        }
    }
    
    // 创建多个sink：文件和控制台
    std::vector<spdlog::sink_ptr> sinks;
    
    // 文件输出sink - 输出到用户指定的日志文件
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path, true);
    sinks.push_back(file_sink);
    
    // 控制台输出sink
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(console_sink);
    
    // 创建logger并注册
    auto logger = std::make_shared<spdlog::logger>("hyperFEM", begin(sinks), end(sinks));
    logger->set_level(log_level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
    
    // --- Step 3: Now use the logger for actual logging ---
    spdlog::info("Logger initialized. Application starting...");
    spdlog::info("Log level set to: {}", spdlog::level::to_string_view(log_level));
    
    // --- Step 4: 模式决策 ---
    // 根据是否提供了 --input-file 来决定进入哪种模式
    if (!input_file_path.empty()) {
        // --- BATCH MODE EXECUTION ---
        spdlog::info("Running in Batch Mode.");
        spdlog::info("Processing input file: {}", input_file_path);
        
        // 创建Mesh对象来存储解析的数据
        Mesh mesh;
        
        // 使用FemParser解析输入文件
        if (FemParser::parse(input_file_path, mesh)) {
            spdlog::info("Successfully parsed input file: {}", input_file_path);
            spdlog::info("Total nodes loaded: {}", mesh.getNodeCount());
            spdlog::info("Total elements loaded: {}", mesh.getElementCount());
            spdlog::info("Total sets loaded: {}", mesh.set_id_to_name.size());
            
            // 在批处理模式下，可以增加更多可选操作，例如
            // if (build_topology_flag) { ... }
            
            // --- Step 5: Export the mesh if an output file is specified ---
            if (!output_file_path.empty()) {
                spdlog::info("Exporting mesh data to: {}", output_file_path);
                if (FemExporter::save(output_file_path, mesh)) {
                    spdlog::info("Successfully exported mesh data.");
                } else {
                    spdlog::error("Failed to export mesh data to: {}", output_file_path);
                    return 1;
                }
            }
            
        } else {
            spdlog::error("Failed to parse input file: {}", input_file_path);
            return 1;
        }
    } else {
        // --- INTERACTIVE MODE EXECUTION ---
        spdlog::info("No input file specified. Running in Interactive Mode.");
        spdlog::info("Type 'help' for a list of commands, 'quit' or 'exit' to leave.");
        
        AppSession session;
        std::string command_line;
        
        while (session.is_running) {
            std::cout << "hyperFEM> " << std::flush;
            if (std::getline(std::cin, command_line)) {
                if (!command_line.empty()) {
                    process_command(command_line, session);
                }
            } else {
                // 处理 Ctrl+D (Unix) 或 Ctrl+Z (Windows) 结束输入
                session.is_running = false;
                std::cout << std::endl; // 换行以保持终端整洁
            }
        }
    }
    
    spdlog::info("Application finished successfully.");
    return 0;
}