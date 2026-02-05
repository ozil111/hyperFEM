// system/exporter_simdroid/SimdroidExporter.cpp

// system/exporter_simdroid/SimdroidExporter.cpp
#include "SimdroidExporter.h"
#include "../../data_center/components/mesh_components.h"
#include "../../data_center/components/material_components.h"
#include "../../data_center/components/simdroid_components.h"
#include "../../data_center/components/analysis_component.h"
#include "../../data_center/TopologyData.h"
#include "spdlog/spdlog.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <array>
#include <tuple>
#include <map>
#include <unordered_map>
#include <memory>


namespace {
    // 将离散的 ID 列表转换为 Simdroid 的 Range 格式字符串 (start:end:step)
    // 支持 step > 1 的压缩，例如 [1,3,5,7,9] -> "1:9:2"
    // 注意：Simdroid 要求至少 3 个元素才能用 range 格式，2 个元素写成单独值
    std::string compress_ids_to_ranges(std::vector<int>& ids) {
        if (ids.empty()) return "";
        
        // 必须先排序
        std::sort(ids.begin(), ids.end());
        
        // 去重（防止重复 set 定义导致重复成员）
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        
        if (ids.size() == 1) return std::to_string(ids[0]);
        
        std::stringstream ss;
        
        // Lambda 用于写入单个值
        auto write_single = [&](int val) {
            if (ss.tellp() > 0) ss << ",";
            ss << val;
        };
        
        // Lambda 用于写入一个 range (仅当 count >= 3 时调用)
        auto write_range = [&](int start_val, int end_val, int step) {
            if (ss.tellp() > 0) ss << ",";
            ss << start_val << ":" << end_val << ":" << step;
        };
        
        size_t i = 0;
        while (i < ids.size()) {
            int start = ids[i];
            
            if (i + 1 >= ids.size()) {
                // 只剩一个元素
                write_single(start);
                break;
            }
            
            int step = ids[i + 1] - ids[i];
            if (step <= 0) {
                // 不应该发生（已排序去重），但保险起见
                write_single(start);
                ++i;
                continue;
            }
            
            // 尝试用当前 step 延伸 range
            int end = start;
            size_t j = i + 1;
            while (j < ids.size() && ids[j] == end + step) {
                end = ids[j];
                ++j;
            }
            
            // 计算 range 内元素个数
            int count = (end - start) / step + 1;
            
            if (count >= 3) {
                // 至少 3 个元素才用 range 格式 (Simdroid 要求)
                write_range(start, end, step);
                i = j;
            } else {
                // 1 或 2 个元素，写成单独的值
                write_single(start);
                ++i;
            }
        }
        
        return ss.str();
    }

    // Simdroid mesh.dat: each line must have <= N commas.
    // This helper wraps comma-separated lists by inserting '\n' when comma count exceeds the limit.
    // Notes:
    // - Newlines are inserted only between tokens (after writing a comma).
    // - `continuation_prefix` is written at the beginning of wrapped lines for readability.
    std::string wrap_csv_tokens_with_comma_limit(const std::string& first_prefix,
                                                 const std::string& continuation_prefix,
                                                 const std::vector<std::string>& tokens,
                                                 int max_commas_per_line,
                                                 const std::string& suffix) {
        std::ostringstream oss;
        oss << first_prefix;
        int comma_count = 0;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) {
                oss << ",";
                ++comma_count;
                if (comma_count >= max_commas_per_line) {
                    oss << "\n" << continuation_prefix;
                    comma_count = 0;
                }
            }
            oss << tokens[i];
        }
        oss << suffix;
        return oss.str();
    }

    std::vector<std::string> split_csv_tokens(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        cur.reserve(s.size());
        for (char c : s) {
            if (c == ',') {
                // trim spaces
                size_t b = 0;
                while (b < cur.size() && std::isspace(static_cast<unsigned char>(cur[b]))) ++b;
                size_t e = cur.size();
                while (e > b && std::isspace(static_cast<unsigned char>(cur[e - 1]))) --e;
                if (e > b) out.emplace_back(cur.substr(b, e - b));
                else out.emplace_back(std::string{});
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        // last
        {
            size_t b = 0;
            while (b < cur.size() && std::isspace(static_cast<unsigned char>(cur[b]))) ++b;
            size_t e = cur.size();
            while (e > b && std::isspace(static_cast<unsigned char>(cur[e - 1]))) --e;
            if (e > b) out.emplace_back(cur.substr(b, e - b));
            else if (!s.empty()) out.emplace_back(std::string{});
        }
        // remove empty tokens (defensive)
        out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& t) { return t.empty(); }), out.end());
        return out;
    }
}

bool SimdroidExporter::save(const std::string& mesh_path, const std::string& control_path, DataContext& ctx) {
    try {
        spdlog::info("Exporting Simdroid Mesh to: {}", mesh_path);
        save_mesh_dat(mesh_path, ctx.registry);

        spdlog::info("Exporting Simdroid Control to: {}", control_path);
        save_control_json(control_path, ctx);
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Simdroid Export Failed: {}", e.what());
        return false;
    }
}

// =============================================================================
// 1. Mesh Export (完全逆运算)
// =============================================================================
void SimdroidExporter::save_mesh_dat(const std::string& path, entt::registry& registry) {
    std::ofstream file(path);
    if (!file.is_open()) throw std::runtime_error("Cannot create mesh file");

    file.precision(10); // 保证坐标精度

    // --- 0. 先统计数量，用于写 Stat 块 ---
    // 用单组件 view 取 size（多组件 view 某些 EnTT 版本没有 size()）
    size_t node_count = registry.view<Component::Position>().size();
    auto elem_view = registry.view<const Component::ElementType, const Component::OriginalID, const Component::Connectivity>();
    size_t element_count = 0;
    for (auto e : elem_view) { (void)e; ++element_count; }

    // Simdroid 对 Element/Surface 的“ID”严格来说是 index：
    // - Element 必须从 0 开始连续编号
    // - Surface 必须与 Element 使用同一套全局编号空间（通常紧接 Element 编号）
    //
    // 因此这里建立 OriginalID -> index 的映射，用于导出与所有 Set/Surface 引用。
    std::unordered_map<int, int> element_id_to_index;
    // 显式 Surface 实体（来自 simdroid 导入）重编号后的映射：old SurfaceID -> new global index
    // 仅在 surf_id_view 分支有意义；若无显式 Surface，则保持为空。
    std::unordered_map<int, int> surface_old_to_new;
    {
        std::vector<std::pair<int, entt::entity>> elems;
        elems.reserve(element_count);
        for (auto e : elem_view) {
            elems.emplace_back(elem_view.get<const Component::OriginalID>(e).value, e);
        }
        std::sort(elems.begin(), elems.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

        element_id_to_index.reserve(elems.size());
        for (int i = 0; i < static_cast<int>(elems.size()); ++i) {
            element_id_to_index.emplace(elems[i].first, i);
        }
    }
    
    auto node_view = registry.view<const Component::Position, const Component::OriginalID>();
    // Surfaces 数量:
    // - 优先使用已存在的 Surface 实体（来自 Simdroid mesh.dat 解析），以保证 SurfaceID 与 SurfaceSet 对齐。
    //   这里仅统计 parent element 仍然有效的 surface，避免导出脏数据。
    // - 否则如果 TopologyData 可用则统计边界面数量（face_to_elements.size()==1）。
    size_t surface_count = 0;
    {
        auto surf_view = registry.view<const Component::SurfaceID, const Component::SurfaceParentElement>();
        for (auto se : surf_view) {
            const auto parent = surf_view.get<const Component::SurfaceParentElement>(se).element;
            // 需要 parent element 可导出（至少有 OriginalID），否则 Surface 会在写出阶段被过滤掉
            if (registry.valid(parent) && registry.all_of<Component::OriginalID>(parent)) {
                const int parent_old_eid = registry.get<Component::OriginalID>(parent).value;
                if (element_id_to_index.find(parent_old_eid) != element_id_to_index.end()) ++surface_count;
            }
        }
    }
    if (registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
        auto& topo = *registry.ctx().get<std::unique_ptr<TopologyData>>();
        if (surface_count == 0) {
            for (size_t face_id = 0; face_id < topo.faces.size(); ++face_id) {
                if (face_id < topo.face_to_elements.size() && topo.face_to_elements[face_id].size() == 1) {
                    ++surface_count;
                }
            }
        }
    }

    // --- Stat 块 (Simdroid mesh.dat 开头) ---
    file << "Stat {\n";
    file << "    Nodes " << node_count << "\n";
    file << "    Elements " << element_count << "\n";
    file << "    Surfaces " << surface_count << "\n";
    file << "}\n\n";

    // --- 1. Nodes ---
    file << "Node {\n";
    // 为了美观，建议按 ID 排序输出（可选）
    std::map<int, const Component::Position*> sorted_nodes;
    for (auto entity : node_view) {
        sorted_nodes[node_view.get<const Component::OriginalID>(entity).value] = &node_view.get<const Component::Position>(entity);
    }
    
    for (const auto& [id, pos] : sorted_nodes) {
        // Format: ID [x,y,z]  (无逗号分隔 ID 和方括号)
        file << "    " << id << " [" << pos->x << "," << pos->y << "," << pos->z << "]\n";
    }
    file << "}\n\n";

    // --- 2. Elements (需要按类型分组) ---
    // Simdroid 格式: Element { Hex8 { ... } Tet4 { ... } }
    file << "Element {\n";
    
    // 映射: TypeName -> List of Lines
    std::map<std::string, std::vector<std::string>> elements_by_type;

    for (auto entity : elem_view) {
        int type_id = elem_view.get<const Component::ElementType>(entity).type_id;
        const int old_eid = elem_view.get<const Component::OriginalID>(entity).value;
        auto it_eid = element_id_to_index.find(old_eid);
        const int eid = (it_eid == element_id_to_index.end()) ? 0 : it_eid->second;
        const auto& conn = elem_view.get<const Component::Connectivity>(entity);

        // 转换节点 Entity -> Original ID
        std::vector<int> node_ids;
        for (auto ne : conn.nodes) {
            node_ids.push_back(registry.get<Component::OriginalID>(ne).value);
        }

        // 类型 ID 转 Simdroid 字符串
        std::string type_name;
        switch(type_id) {
            case 308: type_name = "Hex8"; break;
            case 304: type_name = "Tet4"; break; // 注意: Simdroid 可能区分 Tet4 和 Quad4，需根据逻辑区分
            case 310: type_name = "Tet10"; break;
            case 204: type_name = "Quad4"; break;
            case 203: type_name = "Tri3"; break;
            case 102: type_name = "Line2"; break;
            default:  type_name = "Unknown_" + std::to_string(type_id); break;
        }

        // 构建行: ID [n1,n2,...]  (无逗号分隔 ID 和方括号)
        std::vector<std::string> toks;
        toks.reserve(node_ids.size());
        for (int nid : node_ids) toks.emplace_back(std::to_string(nid));
        const std::string first_prefix = "    " + std::to_string(eid) + " [";
        // Continuation indentation: keep it simple & stable; Simdroid parser treats whitespace as separators.
        const std::string line = wrap_csv_tokens_with_comma_limit(first_prefix, "      ", toks, 10, "]");
        
        elements_by_type[type_name].push_back(line);
    }

    // 写入分组
    for (const auto& [type_name, lines] : elements_by_type) {
        file << "  " << type_name << " {\n";
        for (const auto& line : lines) {
            file << line << "\n";
        }
        file << "  }\n";
    }
    file << "}\n\n";

    // --- 2.5 Surface 块 (边界面/边) ---
    // Simdroid 格式: Surface { Line2 { ... } Tri3 { ... } Tri6 { ... } Quad4 { ... } }
    //
    // 优先策略：若 registry 中存在 Surface 实体，则按 SurfaceID 原样导出（保证与 SurfaceSet 对齐）。
    // 否则：从 TopologyData 反算边界面/边并导出。
    auto surf_id_view = registry.view<const Component::SurfaceID>();
    auto surf_view = registry.view<const Component::SurfaceID, const Component::SurfaceConnectivity, const Component::SurfaceParentElement>();
    if (surf_id_view.size() > 0) {
        // Surface 需要重建为全局连续 index（紧接 Element index），并同步 SurfaceSet 的引用
        // 为了尽可能“最小惊讶”，我们按旧的 SurfaceID 排序后再重新编号。
        struct SurfRec {
            int old_sid;
            int new_sid;
            std::vector<int> node_ids;
            int parent_eidx;
        };
        std::vector<SurfRec> surf_recs;
        surf_recs.reserve(surf_id_view.size());

        std::vector<std::tuple<int, std::vector<int>, int>> line2_surfs;
        std::vector<std::tuple<int, std::vector<int>, int>> tri3_surfs;
        std::vector<std::tuple<int, std::vector<int>, int>> tri6_surfs;
        std::vector<std::tuple<int, std::vector<int>, int>> quad4_surfs;
        for (auto se : surf_view) {
            const int old_sid = surf_view.get<const Component::SurfaceID>(se).value;
            const auto& sc = surf_view.get<const Component::SurfaceConnectivity>(se).nodes;
            const auto& pe = surf_view.get<const Component::SurfaceParentElement>(se).element;

            // Skip invalid parent (e.g., after delete_part)
            if (!registry.valid(pe) || !registry.all_of<Component::OriginalID>(pe)) continue;
            const int parent_old_eid = registry.get<Component::OriginalID>(pe).value;
            auto it_parent = element_id_to_index.find(parent_old_eid);
            if (it_parent == element_id_to_index.end()) continue;
            const int parent_eidx = it_parent->second;

            std::vector<int> node_ids;
            node_ids.reserve(sc.size());
            for (auto ne : sc) {
                if (registry.valid(ne) && registry.all_of<Component::OriginalID>(ne)) {
                    node_ids.push_back(registry.get<Component::OriginalID>(ne).value);
                }
            }

            surf_recs.push_back(SurfRec{old_sid, -1, std::move(node_ids), parent_eidx});
        }

        std::sort(surf_recs.begin(), surf_recs.end(), [](const SurfRec& a, const SurfRec& b) {
            return a.old_sid < b.old_sid;
        });

        int next_sid = static_cast<int>(element_count);
        surface_old_to_new.reserve(surf_recs.size());
        for (auto& r : surf_recs) {
            r.new_sid = next_sid++;
            surface_old_to_new.emplace(r.old_sid, r.new_sid);

            auto& bucket =
                (r.node_ids.size() == 2) ? line2_surfs :
                (r.node_ids.size() == 3) ? tri3_surfs :
                (r.node_ids.size() == 4) ? quad4_surfs :
                (r.node_ids.size() == 6) ? tri6_surfs :
                tri3_surfs; // fallback: treat as Tri3-like for unknown sizes

            bucket.emplace_back(r.new_sid, std::move(r.node_ids), r.parent_eidx);
        }

        auto by_sid = [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); };
        std::sort(line2_surfs.begin(), line2_surfs.end(), by_sid);
        std::sort(tri3_surfs.begin(), tri3_surfs.end(), by_sid);
        std::sort(tri6_surfs.begin(), tri6_surfs.end(), by_sid);
        std::sort(quad4_surfs.begin(), quad4_surfs.end(), by_sid);

        auto write_surface_bucket = [&](const char* header, const auto& bucket) {
            if (bucket.empty()) return;
            file << "  " << header << " {\n";
            for (const auto& [sid, nodes, parent_eid] : bucket) {
                std::vector<std::string> toks;
                toks.reserve(nodes.size() + 1);
                for (int nid : nodes) toks.emplace_back(std::to_string(nid));
                toks.emplace_back(std::to_string(parent_eid));
                const std::string first_prefix = "    " + std::to_string(sid) + " [";
                file << wrap_csv_tokens_with_comma_limit(first_prefix, "      ", toks, 10, "]") << "\n";
            }
            file << "  }\n";
        };

        if (!line2_surfs.empty() || !tri3_surfs.empty() || !tri6_surfs.empty() || !quad4_surfs.empty()) {
            file << "Surface {\n";
            write_surface_bucket("Line2", line2_surfs);
            write_surface_bucket("Tri3", tri3_surfs);
            write_surface_bucket("Tri6", tri6_surfs);
            write_surface_bucket("Quad4", quad4_surfs);
            file << "}\n\n";
        }

    } else if (registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
        auto& topo = *registry.ctx().get<std::unique_ptr<TopologyData>>();
        
        // 收集边界面/边（只被一个单元引用）
        // 按节点数量分类: 2节点=Line2, 3节点=Tri3/Tri6(取决于父单元), 4节点=Quad4
        std::vector<std::string> line2_lines;
        std::vector<std::string> tri3_lines;
        std::vector<std::string> tri6_lines;
        std::vector<std::string> quad4_lines;
        
        int surface_id = static_cast<int>(element_count); // Surface ID 紧接 Element ID
        
        for (size_t face_id = 0; face_id < topo.faces.size(); ++face_id) {
            const auto& face_elements = topo.face_to_elements[face_id];
            if (face_elements.size() != 1) continue; // 非边界面/边，跳过
            
            const FaceKey& face_nodes = topo.faces[face_id];
            entt::entity parent_entity = face_elements[0];
            
            // 获取父单元的 OriginalID
            int parent_elem_id = 0;
            int parent_type_id = 0;
            if (registry.valid(parent_entity) && registry.all_of<Component::OriginalID>(parent_entity)) {
                const int parent_old_eid = registry.get<Component::OriginalID>(parent_entity).value;
                auto it_parent = element_id_to_index.find(parent_old_eid);
                if (it_parent != element_id_to_index.end()) parent_elem_id = it_parent->second;
            }
            if (registry.valid(parent_entity) && registry.all_of<Component::ElementType>(parent_entity)) {
                parent_type_id = registry.get<Component::ElementType>(parent_entity).type_id;
            }
            
            // 构建行: ID [n1,n2,...,parent_elem_id]
            auto push_surface_line = [&](std::vector<std::string>& out, const std::vector<int>& nodes) {
                std::vector<std::string> toks;
                toks.reserve(nodes.size() + 1);
                for (int nid : nodes) toks.emplace_back(std::to_string(nid));
                toks.emplace_back(std::to_string(parent_elem_id));
                const std::string first_prefix = "    " + std::to_string(surface_id) + " [";
                out.push_back(wrap_csv_tokens_with_comma_limit(first_prefix, "      ", toks, 10, "]"));
            };
            
            if (face_nodes.size() == 2) {
                push_surface_line(line2_lines, face_nodes);
            } else if (face_nodes.size() == 3) {
                // Tet10 的边界面需要输出为 Tri6（包含中点节点）
                if (parent_type_id == 310
                    && registry.valid(parent_entity)
                    && registry.all_of<Component::Connectivity>(parent_entity)) {

                    // 从父单元连接性中提取 Tet10 的 10 个节点 ID（保持原始顺序）
                    std::vector<int> parent_node_ids;
                    const auto& parent_conn = registry.get<Component::Connectivity>(parent_entity);
                    parent_node_ids.reserve(parent_conn.nodes.size());
                    for (auto ne : parent_conn.nodes) {
                        parent_node_ids.push_back(registry.get<Component::OriginalID>(ne).value);
                    }

                    // 假设 Tet10 顺序: [0..3]=角点, [4]=01, [5]=12, [6]=20, [7]=03, [8]=13, [9]=23
                    if (parent_node_ids.size() == 10) {
                        const int n0 = parent_node_ids[0];
                        const int n1 = parent_node_ids[1];
                        const int n2 = parent_node_ids[2];
                        const int n3 = parent_node_ids[3];
                        const int m01 = parent_node_ids[4];
                        const int m12 = parent_node_ids[5];
                        const int m20 = parent_node_ids[6];
                        const int m03 = parent_node_ids[7];
                        const int m13 = parent_node_ids[8];
                        const int m23 = parent_node_ids[9];

                        auto sorted3 = [](int a, int b, int c) {
                            std::array<int, 3> t{{a, b, c}};
                            std::sort(t.begin(), t.end());
                            return t;
                        };

                        const auto key = sorted3(face_nodes[0], face_nodes[1], face_nodes[2]);
                        std::vector<int> tri6_nodes;

                        if (key == sorted3(n0, n1, n2)) {
                            tri6_nodes = {n0, n1, n2, m01, m12, m20};
                        } else if (key == sorted3(n0, n3, n1)) {
                            tri6_nodes = {n0, n3, n1, m03, m13, m01};
                        } else if (key == sorted3(n1, n3, n2)) {
                            tri6_nodes = {n1, n3, n2, m13, m23, m12};
                        } else if (key == sorted3(n2, n3, n0)) {
                            tri6_nodes = {n2, n3, n0, m23, m03, m20};
                        }

                        if (!tri6_nodes.empty()) {
                            push_surface_line(tri6_lines, tri6_nodes);
                        } else {
                            // 兜底：如果识别失败，则仍按 Tri3 输出角点面
                            push_surface_line(tri3_lines, face_nodes);
                        }
                    } else {
                        // 兜底：连接性不符合 Tet10 预期，按 Tri3 输出
                        push_surface_line(tri3_lines, face_nodes);
                    }
                } else {
                    push_surface_line(tri3_lines, face_nodes);
                }
            } else if (face_nodes.size() == 4) {
                push_surface_line(quad4_lines, face_nodes);
            }
            // 其他节点数量暂时忽略
            
            ++surface_id;
        }
        
        // 写入 Surface 块
        if (!line2_lines.empty() || !tri3_lines.empty() || !tri6_lines.empty() || !quad4_lines.empty()) {
            file << "Surface {\n";
            if (!line2_lines.empty()) {
                file << "  Line2 {\n";
                for (const auto& line : line2_lines) {
                    file << line << "\n";
                }
                file << "  }\n";
            }
            if (!tri3_lines.empty()) {
                file << "  Tri3 {\n";
                for (const auto& line : tri3_lines) {
                    file << line << "\n";
                }
                file << "  }\n";
            }
            if (!tri6_lines.empty()) {
                file << "  Tri6 {\n";
                for (const auto& line : tri6_lines) {
                    file << line << "\n";
                }
                file << "  }\n";
            }
            if (!quad4_lines.empty()) {
                file << "  Quad4 {\n";
                for (const auto& line : quad4_lines) {
                    file << line << "\n";
                }
                file << "  }\n";
            }
            file << "}\n\n";
        }
    }

    // --- 3. Sets (NodeSet & ElementSet & Parts) ---
    // Simdroid 格式: Set { Node { Name { ranges } } }
    file << "Set {\n";

    // 3.1 Node Sets
    {
        file << "  Node {\n";
        auto view = registry.view<const Component::SetName, const Component::NodeSetMembers>();
        for (auto entity : view) {
            const auto& name = view.get<const Component::SetName>(entity).value;
            const auto& members = view.get<const Component::NodeSetMembers>(entity).members;
            
            // 提取所有成员的 ID
            std::vector<int> ids;
            ids.reserve(members.size());
            for(auto ne : members) {
                if(registry.valid(ne) && registry.all_of<Component::OriginalID>(ne))
                    ids.push_back(registry.get<Component::OriginalID>(ne).value);
            }

            if (!ids.empty()) {
                const std::string ranges = compress_ids_to_ranges(ids);
                const auto toks = split_csv_tokens(ranges);
                const std::string first_prefix = "    " + name + " [";
                file << wrap_csv_tokens_with_comma_limit(first_prefix, "      ", toks, 10, "]") << "\n";
            }
        }
        file << "  }\n";
    }

    // 3.2 Element Sets & Parts
    // 注意: Simdroid 的 Part 本质上是一个 Element Set，但在 'Part {' 块里也会引用。
    // 为了 'mesh.dat' 的完整性，通常 Set 块里包含所有的 Element Set。
    {
        file << "  Element {\n";
        auto view = registry.view<const Component::SetName, const Component::ElementSetMembers>();
        for (auto entity : view) {
            const auto& name = view.get<const Component::SetName>(entity).value;
            const auto& members = view.get<const Component::ElementSetMembers>(entity).members;
            
            std::vector<int> ids;
            for(auto ee : members) {
                if (!registry.valid(ee) || !registry.all_of<Component::OriginalID>(ee)) continue;
                const int old_id = registry.get<Component::OriginalID>(ee).value;
                auto it = element_id_to_index.find(old_id);
                if (it != element_id_to_index.end()) ids.push_back(it->second);
            }

            if (!ids.empty()) {
                const std::string ranges = compress_ids_to_ranges(ids);
                const auto toks = split_csv_tokens(ranges);
                const std::string first_prefix = "    " + name + " [";
                file << wrap_csv_tokens_with_comma_limit(first_prefix, "      ", toks, 10, "]") << "\n";
            }
        }
        file << "  }\n";
    }

    // 3.3 Surface Sets (Simdroid requires for contact definitions)
    {
        std::vector<std::string> lines;
        auto view = registry.view<const Component::SetName, const Component::SurfaceSetMembers>();
        for (auto entity : view) {
            const auto& name = view.get<const Component::SetName>(entity).value;
            const auto& members = view.get<const Component::SurfaceSetMembers>(entity).members;

            std::vector<int> ids;
            ids.reserve(members.size());
            for (auto se : members) {
                if (!registry.valid(se)) continue;
                if (registry.all_of<Component::SurfaceID>(se)) {
                    // 显式 Surface 实体：若发生过重编号，必须使用 new index；
                    // 若没有成功生成映射（例如全部 surface 都被过滤掉），则跳过以避免脏引用。
                    const int old_sid = registry.get<Component::SurfaceID>(se).value;
                    if (surf_id_view.size() > 0) {
                        auto it = surface_old_to_new.find(old_sid);
                        if (it != surface_old_to_new.end()) ids.push_back(it->second);
                        // 找不到映射 => 该 Surface 很可能不会被导出（例如 parent invalid），跳过避免脏引用
                    } else {
                        // 非显式 surface（TopologyData 分支）理论上不会走到这里，保留旧行为
                        ids.push_back(old_sid);
                    }
                } else if (registry.all_of<Component::OriginalID>(se)) {
                    // 兜底：如果 surface 用 OriginalID 存储，保持原行为
                    ids.push_back(registry.get<Component::OriginalID>(se).value);
                }
            }

            if (!ids.empty()) {
                const std::string ranges = compress_ids_to_ranges(ids);
                const auto toks = split_csv_tokens(ranges);
                const std::string first_prefix = "    " + name + " [";
                lines.push_back(wrap_csv_tokens_with_comma_limit(first_prefix, "      ", toks, 10, "]"));
            }
        }

        if (!lines.empty()) {
            file << "  Surface {\n";
            for (const auto& l : lines) file << l << "\n";
            file << "  }\n";
        }
    }

    file << "}\n";
    
    // --- 4. Part Definition (Simdroid 特有) ---
    // Simdroid 格式: Part { Part_1_Title [elem_id_ranges] }
    // 需要把 EleSet 展开成具体的 Element ID range 列表
    file << "\nPart {\n";
    auto part_view = registry.view<const Component::SimdroidPart>();
    int part_index = 1;
    for (auto entity : part_view) {
        const auto& part = part_view.get<const Component::SimdroidPart>(entity);
        
        // 构造 Part 名称: Part_N_Title
        std::string part_key = "Part_" + std::to_string(part_index) + "_" + part.name;
        
        // 获取 EleSet 的成员，展开成 Element ID 列表
        std::vector<int> elem_ids;
        if (registry.valid(part.element_set) && registry.all_of<Component::ElementSetMembers>(part.element_set)) {
            const auto& members = registry.get<Component::ElementSetMembers>(part.element_set).members;
            elem_ids.reserve(members.size());
            for (auto ee : members) {
                if (!registry.valid(ee) || !registry.all_of<Component::OriginalID>(ee)) continue;
                const int old_id = registry.get<Component::OriginalID>(ee).value;
                auto it = element_id_to_index.find(old_id);
                if (it != element_id_to_index.end()) elem_ids.push_back(it->second);
            }
        }
        
        if (!elem_ids.empty()) {
            const std::string ranges = compress_ids_to_ranges(elem_ids);
            const auto toks = split_csv_tokens(ranges);
            const std::string first_prefix = "    " + part_key + " [";
            file << wrap_csv_tokens_with_comma_limit(first_prefix, "      ", toks, 10, "]") << "\n";
        }
        
        ++part_index;
    }
    file << "}\n";
}

// =============================================================================
// 2. Control Export (蓝图覆盖策略)
// =============================================================================
void SimdroidExporter::save_control_json(const std::string& path, DataContext& ctx) {
    // 1. 获取蓝图副本 (深拷贝)
    // 如果蓝图是空的（比如是从 .xfem 导入然后想导出为 simdroid），这里就会是一个空 json 对象
    nlohmann::json output = ctx.simdroid_blueprint;
    if (output.is_null()) output = nlohmann::json::object();

    entt::registry& registry = ctx.registry;

    // --- Helpers for Blueprint pruning (deleted/empty sets) ---
    // Build a quick lookup: SetName -> entity (prefer entities that actually have members).
    std::unordered_map<std::string, entt::entity> set_name_to_entity;
    {
        auto set_view = registry.view<const Component::SetName>();
        for (auto e : set_view) {
            const std::string& n = set_view.get<const Component::SetName>(e).value;
            if (n.empty()) continue;

            const bool is_set_like =
                registry.all_of<Component::NodeSetMembers>(e)
                || registry.all_of<Component::ElementSetMembers>(e)
                || registry.all_of<Component::SurfaceSetMembers>(e);
            auto it = set_name_to_entity.find(n);
            if (it == set_name_to_entity.end()) {
                set_name_to_entity.emplace(n, e);
            } else if (is_set_like) {
                // Prefer a set-like entity over non-set entities (e.g. materials also use SetName)
                const bool old_is_set_like =
                    registry.all_of<Component::NodeSetMembers>(it->second)
                    || registry.all_of<Component::ElementSetMembers>(it->second)
                    || registry.all_of<Component::SurfaceSetMembers>(it->second);
                if (!old_is_set_like) it->second = e;
            }
        }
    }

    auto find_set_entity_by_name = [&](const std::string& set_name) -> entt::entity {
        if (set_name.empty()) return entt::null;
        auto it = set_name_to_entity.find(set_name);
        return (it == set_name_to_entity.end()) ? entt::null : it->second;
    };

    auto set_has_any_valid_member = [&](entt::entity set_entity) -> bool {
        if (!registry.valid(set_entity)) return false;

        if (registry.all_of<Component::NodeSetMembers>(set_entity)) {
            const auto& mem = registry.get<Component::NodeSetMembers>(set_entity).members;
            for (auto e : mem) if (registry.valid(e)) return true;
            return false;
        }

        if (registry.all_of<Component::ElementSetMembers>(set_entity)) {
            const auto& mem = registry.get<Component::ElementSetMembers>(set_entity).members;
            for (auto e : mem) if (registry.valid(e)) return true;
            return false;
        }

        if (registry.all_of<Component::SurfaceSetMembers>(set_entity)) {
            const auto& mem = registry.get<Component::SurfaceSetMembers>(set_entity).members;
            for (auto e : mem) if (registry.valid(e)) return true;
            return false;
        }

        // Set exists but has no member component -> treat as empty (would not be exported in mesh.dat)
        return false;
    };

    auto set_name_exists_and_nonempty = [&](const std::string& set_name) -> bool {
        const entt::entity e = find_set_entity_by_name(set_name);
        if (e == entt::null) return false;
        return set_has_any_valid_member(e);
    };

    // --- Sync Materials ---
    // 策略：遍历 ECS 中的材料，更新或创建 JSON 条目
    auto mat_view = registry.view<const Component::SetName, const Component::LinearElasticParams>();
    for (auto entity : mat_view) {
        std::string name = mat_view.get<const Component::SetName>(entity).value;
        const auto& params = mat_view.get<const Component::LinearElasticParams>(entity);

        // 如果蓝图里没有 Material 块，创建它
        if (!output.contains("Material")) output["Material"] = nlohmann::json::object();
        
        // 访问或创建特定材料的节点
        auto& mat_node = output["Material"][name];
        
        // 更新我们关心的参数
        mat_node["Density"] = params.rho;
        mat_node["MaterialConstants"]["E"] = params.E;
        mat_node["MaterialConstants"]["Nu"] = params.nu;
        
        // 注意：如果蓝图里原本有 "Type": "Elastic"，它会被保留。
        // 如果是新建的，可能需要补充 "Type"
        if (!mat_node.contains("Type")) mat_node["Type"] = "Elastic";
    }

    // --- Sync Analysis Settings ---
    if (ctx.analysis_entity != entt::null && registry.valid(ctx.analysis_entity)) {
        // 假设只有一个 Step，或者我们在 Blueprint 中更新第一个 Step
        // 如果蓝图为空，我们构造基本结构
        if (!output.contains("Step")) output["Step"] = {{"Step-1", nlohmann::json::object()}};
        
        auto& step_node = output["Step"].begin().value(); // 获取第一个 Step

        if (registry.all_of<Component::AnalysisType>(ctx.analysis_entity))
            step_node["Type"] = registry.get<Component::AnalysisType>(ctx.analysis_entity).value;
            
        if (registry.all_of<Component::EndTime>(ctx.analysis_entity))
            step_node["Duration"] = registry.get<Component::EndTime>(ctx.analysis_entity).value;

        // Sync Output Interval
        if (registry.all_of<Component::OutputControl>(ctx.analysis_entity)) {
            step_node["Output"]["Interval"] = registry.get<Component::OutputControl>(ctx.analysis_entity).interval;
        }
    }

    // --- Sync Part Properties ---
    // PartProperty 关联 PartKey(如 Part_1), Title, EleSet, Material, CrossSection...
    //
    // [Blueprint Strategy]
    // - 如果导入过 simdroid，则蓝图里通常已有 "PartProperty": { "Part_1": { "PID":..., "Title":..., ... } }
    //   这里应当“就地更新”对应 Part_* 节点，保留 PID/CrossSection/其他未知字段。
    // - 如果蓝图为空（比如从 .xfem 导出 simdroid），则创建 Part_1/Part_2... 并写入 Title（以及 PID）。
    auto part_view = registry.view<const Component::SimdroidPart>();
    if (!output.contains("PartProperty") || !output["PartProperty"].is_object()) {
        output["PartProperty"] = nlohmann::json::object();
    }

    auto& part_prop = output["PartProperty"];

    // 0) Prune PartProperty entries that no longer exist in ECS
    auto part_exists_in_ecs = [&](const std::string& title, const std::string& ele_set_name) -> bool {
        for (auto e : part_view) {
            const auto& part = part_view.get<const Component::SimdroidPart>(e);
            if (!title.empty() && part.name == title) return true;

            if (!ele_set_name.empty()
                && registry.valid(part.element_set)
                && registry.all_of<Component::SetName>(part.element_set)
                && registry.get<Component::SetName>(part.element_set).value == ele_set_name) {
                return true;
            }
        }
        return false;
    };

    {
        std::vector<std::string> keys_to_remove;
        for (auto it = part_prop.begin(); it != part_prop.end(); ++it) {
            if (!it.value().is_object()) continue;
            const std::string title = it.value().value("Title", "");
            const std::string ele_set = it.value().value("EleSet", "");
            if (!part_exists_in_ecs(title, ele_set)) {
                keys_to_remove.push_back(it.key());
            }
        }
        for (const auto& k : keys_to_remove) part_prop.erase(k);
        if (!keys_to_remove.empty()) {
            spdlog::info("Pruned {} stale PartProperty entries from blueprint.", keys_to_remove.size());
        }
    }

    auto find_partprop_entry = [&](const std::string& title, const std::string& ele_set_name) -> nlohmann::json* {
        // 1) 优先按 Title 匹配（典型：key=Part_1, Title=Component_XX）
        if (!title.empty()) {
            for (auto it = part_prop.begin(); it != part_prop.end(); ++it) {
                if (!it.value().is_object()) continue;
                if (it.value().value("Title", "") == title) return &it.value();
            }
        }
        // 2) 退化按 EleSet 匹配（有些数据里 Title 可能缺失/重复）
        if (!ele_set_name.empty()) {
            for (auto it = part_prop.begin(); it != part_prop.end(); ++it) {
                if (!it.value().is_object()) continue;
                if (it.value().value("EleSet", "") == ele_set_name) return &it.value();
            }
        }
        return nullptr;
    };

    auto next_part_key = [&]() -> std::string {
        int max_id = 0;
        for (auto it = part_prop.begin(); it != part_prop.end(); ++it) {
            const std::string& k = it.key();
            if (k.rfind("Part_", 0) != 0) continue; // not starting with "Part_"
            try {
                const int v = std::stoi(k.substr(5));
                if (v > max_id) max_id = v;
            } catch (...) {}
        }
        return "Part_" + std::to_string(max_id + 1);
    };

    int fallback_pid = 1;
    for (auto entity : part_view) {
        const auto& part = part_view.get<const Component::SimdroidPart>(entity);

        const std::string title = part.name;
        std::string ele_set_name;
        std::string mat_name;
        std::string section_name;

        if (registry.valid(part.element_set) && registry.all_of<Component::SetName>(part.element_set)) {
            ele_set_name = registry.get<Component::SetName>(part.element_set).value;
        }
        if (registry.valid(part.material) && registry.all_of<Component::SetName>(part.material)) {
            mat_name = registry.get<Component::SetName>(part.material).value;
        }
        if (registry.valid(part.section) && registry.all_of<Component::SetName>(part.section)) {
            section_name = registry.get<Component::SetName>(part.section).value;
        }

        nlohmann::json* prop_node = find_partprop_entry(title, ele_set_name);
        if (prop_node == nullptr) {
            const std::string new_key = next_part_key();
            part_prop[new_key] = nlohmann::json::object();
            prop_node = &part_prop[new_key];

            // 新建时尽量贴近 Simdroid 习惯格式
            (*prop_node)["PID"] = fallback_pid++;
            if (!title.empty()) (*prop_node)["Title"] = title;
        }

        // 确保 Title 存在（与正确格式一致），并更新关联关系
        if (!title.empty()) (*prop_node)["Title"] = title;
        if (!ele_set_name.empty()) (*prop_node)["EleSet"] = ele_set_name;
        if (!mat_name.empty()) (*prop_node)["Material"] = mat_name;
        if (!section_name.empty()) (*prop_node)["CrossSection"] = section_name;
    }

    // --- Prune blueprint blocks that reference deleted/empty sets ---
    // 1) Load
    if (output.contains("Load") && output["Load"].is_object()) {
        auto& load_block = output["Load"];
        std::vector<std::string> keys_to_remove;
        for (auto it = load_block.begin(); it != load_block.end(); ++it) {
            if (!it.value().is_object()) continue;
            const auto& v = it.value();

            std::string node_set = v.value("NodeSet", "");
            if (node_set.empty()) node_set = v.value("Set", "");
            const std::string ele_set = v.value("EleSet", "");

            bool ok = true;
            if (!node_set.empty()) ok = set_name_exists_and_nonempty(node_set);
            if (ok && !ele_set.empty()) ok = set_name_exists_and_nonempty(ele_set);

            if (!ok) keys_to_remove.push_back(it.key());
        }
        for (const auto& k : keys_to_remove) load_block.erase(k);
        if (!keys_to_remove.empty()) {
            spdlog::info("Pruned {} invalid Load entries from blueprint.", keys_to_remove.size());
        }
    }

    // 2) Constraint (Boundary, RigidBody/MPC, RigidWall...)
    if (output.contains("Constraint") && output["Constraint"].is_object()) {
        auto& cons = output["Constraint"];

        // Boundary
        if (cons.contains("Boundary") && cons["Boundary"].is_object()) {
            auto& bcs = cons["Boundary"];
            std::vector<std::string> keys_to_remove;
            for (auto it = bcs.begin(); it != bcs.end(); ++it) {
                if (!it.value().is_object()) continue;
                std::string set_name = it.value().value("NodeSet", "");
                if (set_name.empty()) set_name = it.value().value("Set", "");
                if (!set_name.empty() && !set_name_exists_and_nonempty(set_name)) {
                    keys_to_remove.push_back(it.key());
                }
            }
            for (const auto& k : keys_to_remove) bcs.erase(k);
            if (!keys_to_remove.empty()) {
                spdlog::info("Pruned {} invalid Boundary entries from blueprint.", keys_to_remove.size());
            }
        }

        // Rigid body style blocks (support multiple naming variants)
        auto prune_rigid_block = [&](const char* block_name) {
            if (!cons.contains(block_name) || !cons[block_name].is_object()) return;
            auto& rb = cons[block_name];
            std::vector<std::string> keys_to_remove;
            for (auto it = rb.begin(); it != rb.end(); ++it) {
                if (!it.value().is_object()) continue;
                const std::string master = it.value().value("MasterNodeSet", "");
                const std::string slave = it.value().value("SlaveNodeSet", "");
                bool ok = true;
                if (!master.empty()) ok = set_name_exists_and_nonempty(master);
                if (ok && !slave.empty()) ok = set_name_exists_and_nonempty(slave);
                // If either is missing or invalid, remove (keeps JSON clean after part/node deletion)
                if (!ok || master.empty() || slave.empty()) keys_to_remove.push_back(it.key());
            }
            for (const auto& k : keys_to_remove) rb.erase(k);
            if (!keys_to_remove.empty()) {
                spdlog::info("Pruned {} invalid '{}' entries from blueprint.", keys_to_remove.size(), block_name);
            }
        };
        prune_rigid_block("RigidBody");
        prune_rigid_block("NodalRigidBody");
        prune_rigid_block("DistributingCoupling");

        // Rigid wall (optional SecondaryNodes/SlaveNodes set)
        if (cons.contains("RigidWall") && cons["RigidWall"].is_object()) {
            auto& rw = cons["RigidWall"];
            std::vector<std::string> keys_to_remove;
            for (auto it = rw.begin(); it != rw.end(); ++it) {
                if (!it.value().is_object()) continue;
                std::string sec = it.value().value("SecondaryNodes", "");
                if (sec.empty()) sec = it.value().value("SlaveNodes", "");
                if (!sec.empty() && !set_name_exists_and_nonempty(sec)) {
                    keys_to_remove.push_back(it.key());
                }
            }
            for (const auto& k : keys_to_remove) rw.erase(k);
            if (!keys_to_remove.empty()) {
                spdlog::info("Pruned {} invalid RigidWall entries from blueprint.", keys_to_remove.size());
            }
        }
    }

    // 3) Contact: remove ties that reference deleted/empty sets
    if (output.contains("Contact") && output["Contact"].is_object()) {
        auto& contact = output["Contact"];
        std::vector<std::string> keys_to_remove;
        for (auto it = contact.begin(); it != contact.end(); ++it) {
            if (!it.value().is_object()) continue;
            const auto& v = it.value();

            const std::string master_faces = v.value("MasterFaces", "");
            const std::string slave_faces = v.value("SlaveFaces", "");
            const std::string slave_nodes = v.value("SlaveNodes", "");

            bool ok = true;
            if (!master_faces.empty()) ok = set_name_exists_and_nonempty(master_faces);
            if (ok && !slave_faces.empty()) ok = set_name_exists_and_nonempty(slave_faces);
            if (ok && !slave_nodes.empty()) ok = set_name_exists_and_nonempty(slave_nodes);

            if (!ok) keys_to_remove.push_back(it.key());
        }
        for (const auto& k : keys_to_remove) contact.erase(k);
        if (!keys_to_remove.empty()) {
            spdlog::info("Pruned {} invalid Contact entries from blueprint.", keys_to_remove.size());
        }
    }

    // 注意：
    // Load 和 Boundary 的双向同步比较复杂，因为它们在 ECS 中被打散到了节点上。
    // 如果用户没有在 ECS 中增删 Load/BC，蓝图中原有的 Load/BC 会被自动保留（这是蓝图策略的最大优势）。
    // 
    // 如果你在软件中允许用户新增载荷，你需要在这里遍历 Component::NodalLoad 并写入 output["Load"]。
    // 目前版本我们先保证 Material 和 Analysis 的修改能回写。

    // 写入文件
    std::ofstream file(path);
    if (!file.is_open()) throw std::runtime_error("Cannot create control file");
    
    file << output.dump(4); // 4空格缩进，美观打印
}