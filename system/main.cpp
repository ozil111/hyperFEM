/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "parser_base/parserBase.h"       // 旧的 .xfem 解析器（向后兼容）
#include "parser_json/JsonParser.h"       // 新的 JSON 解析器
#include "exporter_base/exporterBase.h"
#include "DataContext.h"                  // 引入ECS数据中心
#include "components/mesh_components.h"   // 引入组件定义
#include "components/analysis_component.h"
#include "TopologyData.h"                 // 引入拓扑数据结构
#include "mesh/TopologySystems.h"         // 引入拓扑逻辑系统
#include "AppSession.h"                   // 引入会话状态机
#include "dof/DofNumberingSystem.h"      // DOF 映射系统
#include "mass/MassSystem.h"             // 质量系统
#include "force/InternalForceSystem.h"   // 内力系统
#include "load/LoadSystem.h"             // 载荷系统
#include "explicit/ExplicitSolver.h"     // 显式求解器
#include "material/mat1/LinearElasticMatrixSystem.h"  // 材料矩阵系统
#include "parser_simdroid/SimdroidParser.h" // Simdroid 解析器
#include "exporter_simdroid/SimdroidExporter.h" // Simdroid 导出器
#include "analysis/GraphBuilder.h"
#include "analysis/MermaidReporter.h"
#include "main0_explicit.h"              // 显式求解器逻辑
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <filesystem>
#include <sstream>
#include <cstdlib>

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
    std::cout << "  --input-file, -i <file>    Specify input file (.xfem or .json/.jsonc)" << std::endl;
    std::cout << "  --output-file, -o <file>   Specify output file (.xfem)" << std::endl;
    std::cout << "  --log-level, -l <level>    Set log level (trace, debug, info, warn, error, critical)" << std::endl;
    std::cout << "  --log-directory, -d <path> Set log file path" << std::endl;
    std::cout << "  --help, -h                 Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Supported Input Formats:" << std::endl;
    std::cout << "  .xfem  - Legacy text format (backward compatible)" << std::endl;
    std::cout << "  .json  - JSON format (recommended)" << std::endl;
    std::cout << "  .jsonc - JSON with comments (recommended)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  hyperFEM_app --input-file case/model.jsonc --output-file case/output.xfem" << std::endl;
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
        spdlog::info("Available commands: import, import_simdroid, export_simdroid, info, build_topology, list_bodies, show_body, list_parts, delete_part, graph, node, elem, save, help, quit");
    }
    else if (command == "import") {
        std::string file_path;
        ss >> file_path;
        if (file_path.empty()) {
            spdlog::error("Usage: import <path_to_file>");
            return;
        }
        
        // 检查文件是否存在
        if (!std::filesystem::exists(file_path)) {
            spdlog::error("File does not exist: {}", file_path);
            return;
        }
        
        session.clear_data();
        spdlog::info("Importing mesh from: {}", file_path);
        
        // 根据文件扩展名自动选择解析器
        std::filesystem::path path(file_path);
        std::string extension = path.extension().string();
        bool parse_success = false;
        
        if (extension == ".json" || extension == ".jsonc") {
            spdlog::info("Detected JSON format, using JsonParser...");
            parse_success = JsonParser::parse(file_path, session.data);
        } else if (extension == ".xfem") {
            spdlog::info("Detected XFEM format, using FemParser (legacy)...");
            parse_success = FemParser::parse(file_path, session.data);
        } else {
            spdlog::error("Unsupported file format: {}. Supported: .json, .jsonc, .xfem", extension);
            return;
        }
        
        if (parse_success) {
            session.mesh_loaded = true;
            // Count entities using views
            auto node_count = session.data.registry.view<Component::Position>().size();
            auto element_count = session.data.registry.view<Component::Connectivity>().size();
            spdlog::info("Successfully imported mesh. {} nodes, {} elements.", node_count, element_count);
        } else {
            spdlog::error("Failed to import mesh from: {}", file_path);
        }
    }
    // =======================================================
    // 新增: Simdroid 导入命令
    // =======================================================
    else if (command == "import_simdroid") {
        std::string control_path_str;
        ss >> control_path_str;
        if (control_path_str.empty()) {
            spdlog::error("Usage: import_simdroid <path_to_control.json>");
            return;
        }

        std::filesystem::path control_path(control_path_str);
        if (!std::filesystem::exists(control_path)) {
            spdlog::error("Control file not found: {}", control_path_str);
            return;
        }

        // 自动推导 mesh.dat 路径 (假设在同级目录)
        std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
        if (!std::filesystem::exists(mesh_path)) {
            spdlog::error("Mesh file not found at expected location: {}", mesh_path.string());
            spdlog::info("Tip: mesh.dat must be in the same directory as control.json");
            return;
        }

        spdlog::info("Importing Simdroid model...");
        spdlog::info("  Control: {}", control_path.string());
        spdlog::info("  Mesh:    {}", mesh_path.string());

        session.clear_data();

        // 调用 Parser
        try {
            if (SimdroidParser::parse(mesh_path.string(), control_path.string(), session.data)) {
                session.mesh_loaded = true;

                // 核心步骤：导入成功后，立即构建 Inspector 索引
                session.inspector.build(session.data.registry);

                spdlog::info("Simdroid import successful. Entered Simdroid Interactive Mode.");
            } else {
                spdlog::error("Simdroid import failed.");
            }
        } catch (const std::exception& e) {
            spdlog::error("Exception during import: {}", e.what());
        }
    }
    // =======================================================
    // 新增: Simdroid 导出命令 (Blueprint Strategy)
    // =======================================================
    else if (command == "export_simdroid") {
        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded. Please 'import' or 'import_simdroid' first.");
            return;
        }

        std::string arg1;
        std::string arg2;
        ss >> arg1 >> arg2;

        if (arg1.empty()) {
            spdlog::error("Usage: export_simdroid <output_dir | mesh.dat | control.json> [control.json]");
            return;
        }

        try {
            std::filesystem::path mesh_path;
            std::filesystem::path control_path;

            if (!arg2.empty()) {
                // 显式指定两个路径: export_simdroid <mesh.dat> <control.json>
                mesh_path = std::filesystem::path(arg1);
                control_path = std::filesystem::path(arg2);
            } else {
                // 只给一个参数时，按扩展名推导
                std::filesystem::path out(arg1);
                const std::string ext = out.extension().string();

                if (ext == ".json" || ext == ".jsonc") {
                    control_path = out;
                    mesh_path = out.parent_path() / "mesh.dat";
                } else if (ext == ".dat") {
                    mesh_path = out;
                    control_path = out.parent_path() / "control.json";
                } else {
                    // 当作输出目录
                    mesh_path = out / "mesh.dat";
                    control_path = out / "control.json";
                }
            }

            if (!mesh_path.parent_path().empty()) {
                std::filesystem::create_directories(mesh_path.parent_path());
            }
            if (!control_path.parent_path().empty()) {
                std::filesystem::create_directories(control_path.parent_path());
            }

            spdlog::info("Exporting Simdroid project...");
            spdlog::info("  Mesh:    {}", mesh_path.string());
            spdlog::info("  Control: {}", control_path.string());

            if (SimdroidExporter::save(mesh_path.string(), control_path.string(), session.data)) {
                spdlog::info("Simdroid export successful.");
            } else {
                spdlog::error("Simdroid export failed.");
            }

        } catch (const std::exception& e) {
            spdlog::error("Exception during export: {}", e.what());
        }
    }
    else if (command == "build_topology") {
        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded. Please 'import' a mesh first.");
            return;
        }
        spdlog::info("Building topology data...");
        TopologySystems::extract_topology(session.data.registry);
        session.topology_built = true;
        
        // Get the topology from context to report statistics
        auto& topology = *session.data.registry.ctx().get<std::unique_ptr<TopologyData>>();
        spdlog::info("Topology built successfully. Found {} unique faces.", topology.faces.size());
    }
    else if (command == "list_bodies") {
        if (!session.topology_built) {
            spdlog::error("Topology not built. Please run 'build_topology' first.");
            return;
        }
        spdlog::info("Finding continuous bodies...");
        TopologySystems::find_continuous_bodies(session.data.registry);
        
        // Get the topology from context
        auto& topology = *session.data.registry.ctx().get<std::unique_ptr<TopologyData>>();
        spdlog::info("Found {} continuous body/bodies:", topology.body_to_elements.size());
        for (const auto& pair : topology.body_to_elements) {
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

        // Get the topology from context
        auto& topology = *session.data.registry.ctx().get<std::unique_ptr<TopologyData>>();
        
        // Check if the requested BodyID exists
        auto it = topology.body_to_elements.find(body_id_to_show);
        if (it == topology.body_to_elements.end()) {
            spdlog::error("Body with ID {} not found. Use 'list_bodies' to see available bodies.", body_id_to_show);
            return;
        }

        const std::vector<entt::entity>& element_entities = it->second;
        
        // Build the output list
        std::stringstream element_list_ss;
        for (size_t i = 0; i < element_entities.size(); ++i) {
            entt::entity elem_entity = element_entities[i];
            // Get the OriginalID component to display external ID
            const auto& orig_id = session.data.registry.get<Component::OriginalID>(elem_entity);
            element_list_ss << orig_id.value << (i == element_entities.size() - 1 ? "" : ", ");
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
        if (FemExporter::save(file_path, session.data)) {
            spdlog::info("Successfully exported mesh data.");
        } else {
            spdlog::error("Failed to export mesh data to: {}", file_path);
        }
    }
    else if (command == "info") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded.");
        } else {
            // Count entities using views
            auto node_count = session.data.registry.view<Component::Position>().size();
            auto element_count = session.data.registry.view<Component::Connectivity>().size();
            auto set_count = session.data.registry.view<Component::SetName>().size();
            
            spdlog::info("Mesh loaded: {} nodes, {} elements, {} sets",
                         node_count, element_count, set_count);
            
            if (session.topology_built) {
                auto& topology = *session.data.registry.ctx().get<std::unique_ptr<TopologyData>>();
                spdlog::info("Topology built: {} unique faces, {} bodies",
                             topology.faces.size(),
                             topology.body_to_elements.size());
            } else {
                spdlog::info("Topology not built yet.");
            }
        }
    }
    // =======================================================
    // 新增: Simdroid 交互式调试面板
    // =======================================================
    else if (command == "list_parts") {
        if (!session.mesh_loaded) { spdlog::warn("No mesh loaded."); return; }
        session.inspector.list_parts(session.data.registry);
    }
    else if (command == "delete_part") {
        std::vector<std::string> part_names;
        for (std::string name; ss >> name;) {
            part_names.push_back(name);
        }
        if (part_names.empty()) {
            spdlog::error("Usage: delete_part <part_name> [part_name2 ...]");
            return;
        }

        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded. Please 'import_simdroid' first.");
            return;
        }

        size_t deleted = 0;
        size_t failed = 0;
        for (const auto& part_name : part_names) {
            // delete_part() 会清空 inspector 索引；为保证多次删除稳定，这里每次都先重建索引
            session.inspector.build(session.data.registry);

            if (session.inspector.delete_part(session.data.registry, part_name)) {
                spdlog::info("Part '{}' deleted successfully.", part_name);
                ++deleted;
            } else {
                spdlog::error("Failed to delete part '{}'. Part not found?", part_name);
                ++failed;
            }
        }

        if (deleted > 0) {
            // 删除后必须重建索引，否则 eid_to_part 等映射会失效导致 Crash
            session.inspector.build(session.data.registry);
            // 拓扑数据会因实体删除而失效，清理以避免后续误用
            if (session.data.registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
                session.data.registry.ctx().erase<std::unique_ptr<TopologyData>>();
            }
            session.topology_built = false;
        }
        spdlog::info("delete_part done. Deleted={}, Failed={}", deleted, failed);
    }
    else if (command == "graph") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded.");
            return;
        }

        std::string output_filename;
        ss >> output_filename;
        if (output_filename.empty()) output_filename = "connectivity.html";

        spdlog::info("Analyzing connectivity...");

        // 1. 构建图
        PartGraph graph = GraphBuilder::build(session.data.registry, session.inspector);

        // 2. (可选) 进行一些统计分析，比如打印孤立节点
        int isolated_count = 0;
        for (const auto& [n, node] : graph.nodes) {
            if (node.edges.empty()) isolated_count++;
        }
        spdlog::info("Analysis complete. Parts: {}, Isolated: {}", graph.nodes.size(), isolated_count);

        // 3. 生成报告
        MermaidReporter::generate_interactive_html(graph, output_filename);

        // 4. (可选) 尝试自动打开浏览器 (Windows/Linux/Mac)
#ifdef _WIN32
        std::string cmd = "start " + output_filename;
        system(cmd.c_str());
#endif
    }
    else if (command == "node") {
        int nid;
        if (ss >> nid) {
            session.inspector.inspect_node(session.data.registry, nid);
        } else {
            spdlog::error("Usage: node <node_id>");
        }
    }
    else if (command == "elem" || command == "element") {
        int eid;
        if (ss >> eid) {
            session.inspector.inspect_element(session.data.registry, eid);
        } else {
            spdlog::error("Usage: elem <element_id>");
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
                
                // 验证文件扩展名（支持 .xfem, .json, .jsonc）
                std::filesystem::path file_path(input_file_path);
                std::string extension = file_path.extension().string();
                if (extension != ".xfem" && extension != ".json" && extension != ".jsonc") {
                    std::cerr << "Error: Input file must have .xfem, .json, or .jsonc extension" << std::endl;
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
        
        // 创建DataContext对象来存储解析的数据
        DataContext data_context;
        
        // 根据文件扩展名自动选择解析器
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
            }
            
            // --- Step 6: Export the mesh if an output file is specified ---
            if (!output_file_path.empty()) {
                spdlog::info("Exporting mesh data to: {}", output_file_path);
                if (FemExporter::save(output_file_path, data_context)) {
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