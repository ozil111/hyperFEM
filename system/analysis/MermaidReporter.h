/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once
#include "PartGraph.h"
#include "GraphAnalyzer.h"
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <cctype>

class MermaidReporter {
public:
    static void generate_interactive_html(const PartGraph& graph, const std::string& output_path) {
        // 先进行分析，把图拆解
        auto analysis = GraphAnalyzer::analyze(graph);

        std::ofstream file(output_path);
        if (!file.is_open()) return;

        // 写入 HTML 头部，引入 svg-pan-zoom 库
        file << R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Structure Analysis</title>
    <script src="https://cdn.jsdelivr.net/npm/mermaid/dist/mermaid.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/svg-pan-zoom@3.6.1/dist/svg-pan-zoom.min.js"></script>
    <style>
        body, html { height: 100%; margin: 0; overflow: hidden; font-family: sans-serif; }
        #container { height: 100%; width: 100%; border: 1px solid #ccc; background-color: #fafafa; }
        .controls { position: absolute; top: 10px; right: 10px; z-index: 100; background: white; padding: 10px; border: 1px solid #ccc; border-radius: 5px; box-shadow: 0 2px 5px rgba(0,0,0,0.2); }
    </style>
</head>
<body>
    <div class="controls">
        <h3>Graph Controls</h3>
        <p>Scroll to Zoom, Drag to Pan</p>
        <button onclick="resetZoom()">Reset View</button>
    </div>
    <div id="container" class="mermaid">
graph LR
    %% 全局样式
    classDef load fill:#ffcccc,stroke:#ff0000,stroke-width:3px;
    classDef fix fill:#e6ccff,stroke:#800080,stroke-width:3px;
    classDef normal fill:#f9f9f9,stroke:#333,stroke-width:1px;
    classDef critical stroke:#ff0000,stroke-width:2px,stroke-dasharray: 5 5;
        )HTML";

        // --- 生成 Mermaid 内容 ---
        
        std::unordered_set<std::string> drawn_nodes;

        // 1. 优先绘制“主传力系统” (包含 Load/Constraint 的组)
        int cluster_id = 0;
        for (const auto& component : analysis.components) {
            bool is_main_system = GraphAnalyzer::has_load_or_fix(graph, component);
            
            // 如果是孤立的小零件（少于3个且不重要），可以选择忽略，或者放入"碎片"篮子
            if (!is_main_system && component.size() < 2) continue;

            file << "\n    subgraph Cluster_" << cluster_id++ << " [" << (is_main_system ? "Main Force Path" : "Isolated Assembly") << "]\n";
            file << "    direction TB\n"; // 子图内部从上到下，整体从左到右

            for (const auto& node_name : component) {
                const auto& node = graph.nodes.at(node_name);
                std::string id = sanitize_id(node_name);
                
                // 节点定义
                file << "    " << id << "[\"" << node_name << "\"]\n";
                
                // 样式应用
                if (node.is_load_part) file << "    class " << id << " load;\n";
                else if (node.is_constraint_part) file << "    class " << id << " fix;\n";
                else file << "    class " << id << " normal;\n";

                drawn_nodes.insert(node_name);
            }
            file << "    end\n"; // End subgraph
        }

        // 2. 绘制边 (Edges)
        // 注意：只绘制两个端点都已经被画出来的边，避免引用不存在的节点导致 mermaid 崩溃
        for (const auto& [name, node] : graph.nodes) {
            if (drawn_nodes.find(name) == drawn_nodes.end()) continue;

            std::string src_id = sanitize_id(name);
            for (const auto& edge : node.edges) {
                if (drawn_nodes.find(edge.target_part) == drawn_nodes.end()) continue;

                std::string tgt_id = sanitize_id(edge.target_part);
                if (src_id >= tgt_id) continue; // 去重，只画单向

                file << "    " << src_id;
                
                // 根据连接类型使用不同的 Mermaid 语法
                switch (edge.type) {
                    case ConnectionType::Contact:
                        // 粗实线，带 Contact 标签
                        file << " ===|Contact| ";
                        break;
                    case ConnectionType::SharedNode:
                        // 虚线，带 Shared 标签和数量
                        file << " -.-|\"Shared<br/>(" << edge.count << " nodes)\"| ";
                        break;
                    case ConnectionType::MPC:
                        // 粗箭头
                        file << " ==>|MPC| ";
                        break;
                }
                
                file << tgt_id << "\n";
            }
        }

        // --- 写入 JS 脚本进行缩放增强 ---
        file << R"(
    </div>
    <script>
        var panZoomInstance;
        mermaid.initialize({ 
            startOnLoad: true, 
            theme: 'base',
            flowchart: { useMaxWidth: false, htmlLabels: true }
        });

        // 这是一个回调，等待 Mermaid 渲染完毕后注入 PanZoom
        var callback = function() {
            var svg = document.querySelector('#container svg');
            if(svg) {
                svg.style.height = '100%';
                svg.style.width = '100%';
                panZoomInstance = svgPanZoom(svg, {
                    zoomEnabled: true,
                    controlIconsEnabled: false,
                    fit: true,
                    center: true,
                    minZoom: 0.1
                });
            }
        };

        // 轮询检查 mermaid 是否渲染完成
        var checkExist = setInterval(function() {
           if (document.querySelector('#container svg')) {
              clearInterval(checkExist);
              callback();
           }
        }, 100);

        function resetZoom() {
            if(panZoomInstance) {
                panZoomInstance.reset();
                panZoomInstance.fit();
                panZoomInstance.center();
            }
        }
    </script>
</body>
</html>
        )";

        file.close();
        spdlog::info("Interactive graph report generated at: {}", output_path);
    }

private:
    static std::string sanitize_id(const std::string& name) {
        std::string out = name;
        for (char& c : out) {
            if (!isalnum(c)) c = '_';
        }
        if (isdigit(out[0])) out = "P_" + out;
        return out;
    }
};