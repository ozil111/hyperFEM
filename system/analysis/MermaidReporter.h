/**
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang
 * High-performance topology report generation using Vis.js.
 */
#pragma once
#include "PartGraph.h"
#include "GraphAnalyzer.h"
#include <fstream>
#include <sstream>
#include <unordered_set>

class MermaidReporter {
public:
    static void generate_interactive_html(const PartGraph& graph, const std::string& output_path) {
        auto analysis = GraphAnalyzer::analyze(graph);
        std::ofstream file(output_path);
        if (!file.is_open()) return;

        // Write HTML head and Vis.js CDN script
        file << R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>NovaFEA Structure Analysis</title>
    <script type="text/javascript" src="https://unpkg.com/vis-network/standalone/umd/vis-network.min.js"></script>
    <style>
        body, html { height: 100%; margin: 0; overflow: hidden; background-color: #f4f4f9; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }
        #network-container { height: 100vh; width: 100vw; }
        .legend { position: absolute; top: 10px; left: 10px; z-index: 10; background: rgba(255,255,255,0.9); padding: 15px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); border: 1px solid #ddd; }
        .legend-item { margin: 5px 0; display: flex; align-items: center; font-size: 12px; }
        .color-box { width: 15px; height: 15px; margin-right: 10px; border-radius: 3px; }
    </style>
</head>
<body>
    <div class="legend">
        <strong>NovaFEA legend</strong>
        <div class="legend-item"><div class="color-box" style="background:#ffcccc; border:1px solid #ff0000;"></div> Load node</div>
        <div class="legend-item"><div class="color-box" style="background:#e6ccff; border:1px solid #800080;"></div> Constraint (fixed) node</div>
        <div class="legend-item"><div class="color-box" style="background:#d2e5ff; border:1px solid #2b7ce9;"></div> Standard part</div>
        <hr/>
        <div class="legend-item">Thick line: Tie connection</div>
        <div class="legend-item">Dashed line: Shared nodes</div>
        <div class="legend-item"><div class="color-box" style="background:transparent; border:2px dashed #27ae60;"></div> Green dashed: General contact (auto)</div>
    </div>
    <div id="network-container"></div>

    <script type="text/javascript">
        var nodes = new vis.DataSet([
)HTML";

        // 1. Emit node data (JSON)
        bool first = true;
        for (const auto& [name, node] : graph.nodes) {
            if (!first) file << ",\n";
            std::string color = "#d2e5ff";
            std::string borderColor = "#2b7ce9";
            
            if (node.is_load_part) { color = "#ffcccc"; borderColor = "#ff0000"; }
            else if (node.is_constraint_part) { color = "#e6ccff"; borderColor = "#800080"; }

            std::string tooltip;
            if (!node.material_info.empty()) {
                tooltip += "Material: " + node.material_info;
            }
            if (!node.property_info.empty()) {
                if (!tooltip.empty()) tooltip += "\\n";
                tooltip += "Property: " + node.property_info;
            }

            file << "            { id: \"" << sanitize_id(name) << "\", label: \"" << name << "\", "
                 << "color: { background: '" << color << "', border: '" << borderColor << "' }, "
                 << "borderWidth: 2, "
                 << "title: '" << js_escape(tooltip.empty() ? name : tooltip) << "', "
                 << "materialInfo: '" << js_escape(node.material_info) << "', "
                 << "propertyInfo: '" << js_escape(node.property_info) << "' }";
            first = false;
        }

        file << R"HTML(
        ]);

        var edges = new vis.DataSet([
)HTML";

        // 2. Emit edge data
        first = true;
        for (const auto& [name, node] : graph.nodes) {
            std::string src_id = sanitize_id(name);
            for (const auto& edge : node.edges) {
                std::string tgt_id = sanitize_id(edge.target_part);
                if (src_id >= tgt_id) continue;

                if (!first) file << ",\n";
                file << "            { from: \"" << src_id << "\", to: \"" << tgt_id << "\", ";

                // Edge style by connection type
                if (edge.type == ConnectionType::Contact) {
                    if (edge.sub_type == "Tie") {
                        file << "label: 'Tie', width: 4, color: '#e67e22'";
                    } else if (edge.sub_type == "GeneralContact") {
                        file << "label: 'GeneralContact', width: 2, color: '#27ae60', dashes: [8,4]";
                    } else {
                        file << "label: 'Contact', width: 2, color: '#2980b9'";
                    }
                } else if (edge.type == ConnectionType::SharedNode) {
                    file << "label: 'Shared (" << edge.count << ")', dashes: true, color: '#7f8c8d'";
                } else {
                    file << "label: 'MPC', arrows: 'to', width: 2, color: '#333'";
                }
                file << " }";
                first = false;
            }
        }

        // 3. Vis.js options and initialization
        file << R"HTML(
        ]);

        var container = document.getElementById('network-container');
        var data = { nodes: nodes, edges: edges };
        var options = {
            nodes: { shape: 'box', margin: 10, font: { size: 14 } },
            edges: { font: { align: 'middle', size: 10 } },
            physics: {
                enabled: true,
                barnesHut: { gravitationalConstant: -2000, centralGravity: 0.3, springLength: 150 },
                stabilization: { iterations: 100 }
            },
            interaction: { hover: true, navigationButtons: true, keyboard: true }
        };
        var network = new vis.Network(container, data, options);

        // On node click, show material and property info
        network.on('click', function (params) {
            if (params.nodes.length === 0) return;
            var nodeId = params.nodes[0];
            var node = nodes.get(nodeId);
            if (!node) return;

            var lines = [];
            lines.push('Part: ' + node.label);
            if (node.materialInfo) {
                lines.push('Material: ' + node.materialInfo);
            }
            if (node.propertyInfo) {
                lines.push('Property: ' + node.propertyInfo);
            }
            alert(lines.join('\\n'));
        });
    </script>
</body>
</html>
)HTML";

        file.close();
    }

private:
    static std::string sanitize_id(const std::string& name) {
        std::string out = name;
        for (char& c : out) if (!isalnum(c)) c = '_';
        if (isdigit(out[0])) out = "P_" + out;
        return out;
    }
    
    static std::string js_escape(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        for (char c : input) {
            if (c == '\\\\') out += "\\\\";
            else if (c == '\'') out += "\\'";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') { /* skip */ }
            else out += c;
        }
        return out;
    }
};