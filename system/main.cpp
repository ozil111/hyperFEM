#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "parser_base/parserBase.h"
#include "mesh.h"
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <filesystem>

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
    std::cout << "  --log-level, -l <level>    Set log level (trace, debug, info, warn, error, critical)" << std::endl;
    std::cout << "  --log-directory, -d <path> Set log file path" << std::endl;
    std::cout << "  --help, -h                 Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  hyperFEM_app --input-file case/node.xfem" << std::endl;
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
    
    // --- Step 4: Process input file if provided ---
    if (!input_file_path.empty()) {
        spdlog::info("Processing input file: {}", input_file_path);
        
        // 创建Mesh对象来存储解析的数据
        Mesh mesh;
        
        // 使用FemParser解析输入文件
        if (FemParser::parse(input_file_path, mesh)) {
            spdlog::info("Successfully parsed input file: {}", input_file_path);
            spdlog::info("Total nodes loaded: {}", mesh.getNodeCount());
            spdlog::info("Total elements loaded: {}", mesh.getElementCount());
            
            // 这里可以添加更多的处理逻辑，比如：
            // - 显示网格信息
            // - 进行有限元计算
            // - 输出结果等
            
        } else {
            spdlog::error("Failed to parse input file: {}", input_file_path);
            return 1;
        }
    } else {
        spdlog::warn("No input file specified. Use --input-file to specify a .xfem file to process.");
        spdlog::info("Use --help for usage information.");
    }
    
    spdlog::info("Application finished successfully.");
    return 0;
}