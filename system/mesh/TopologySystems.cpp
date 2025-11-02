// TopologySystems.cpp
#include "TopologySystems.h"
#include <algorithm> // for std::sort
#include <queue>     // for std::queue in flood fill
#include "spdlog/spdlog.h"

// -------------------------------------------------------------------
// **System 1: 拓扑提取**
// -------------------------------------------------------------------
void TopologySystems::extract_topology(const Mesh& source_mesh, TopologyData& topology) {
    topology.clear();
    topology.element_to_faces.resize(source_mesh.getElementCount());

    for (ElementIndex elem_idx = 0; elem_idx < source_mesh.getElementCount(); ++elem_idx) {
        // 1. 获取当前单元的节点 (使用外部ID)
        size_t start = source_mesh.element_offsets[elem_idx];
        size_t end = (elem_idx + 1 < source_mesh.getElementCount())
                       ? source_mesh.element_offsets[elem_idx + 1]
                       : source_mesh.element_connectivity.size();
        std::vector<NodeID> element_nodes(
            source_mesh.element_connectivity.begin() + start,
            source_mesh.element_connectivity.begin() + end
        );
        int elem_type = source_mesh.element_types[elem_idx];

        // 2. 从单元中提取所有的面
        auto element_faces = get_faces_from_element(element_nodes, elem_type);

        for (auto& face_key : element_faces) {
            // 3. 对面的节点ID排序，创建唯一的 FaceKey
            std::sort(face_key.begin(), face_key.end());

            FaceID face_id;
            // 4. 检查这个面是否已经存在
            auto it = topology.face_key_to_id.find(face_key);
            if (it == topology.face_key_to_id.end()) {
                // 如果是新面：注册它，并分配一个新的FaceID
                face_id = topology.faces.size();
                topology.faces.push_back(face_key);
                topology.face_key_to_id[face_key] = face_id;
                topology.face_to_elements.emplace_back(); // 为新面创建一个空的单元列表
            } else {
                // 如果是已存在的面：获取它的FaceID
                face_id = it->second;
            }

            // 5. 构建双向关系
            topology.element_to_faces[elem_idx].push_back(face_id);
            // **优化点**: 存储单元的内部索引 `elem_idx` 而不是外部ID
            topology.face_to_elements[face_id].push_back(elem_idx);
        }
    }
}

// -------------------------------------------------------------------
// **System 2: 连续体查找 (洪水填充)**
// -------------------------------------------------------------------
void TopologySystems::find_continuous_bodies(TopologyData& topology) {
    topology.element_to_body.assign(topology.source_mesh.getElementCount(), -1); // -1代表未访问
    topology.body_to_elements.clear();
    BodyID current_body_id = 0;

    for (ElementIndex i = 0; i < topology.source_mesh.getElementCount(); ++i) {
        if (topology.element_to_body[i] == -1) { // 如果此单元还未被分配到任何连续体
            // 从此单元开始进行一次新的洪水填充
            std::queue<ElementIndex> q;
            q.push(i);
            topology.element_to_body[i] = current_body_id;

            while (!q.empty()) {
                ElementIndex current_elem_idx = q.front();
                q.pop();

                // **优化点**: 在body_to_elements中存储内部索引
                topology.body_to_elements[current_body_id].push_back(current_elem_idx);

                // 通过共享面寻找所有邻居单元
                const auto& faces_of_current_elem = topology.element_to_faces[current_elem_idx];
                for (FaceID face_id : faces_of_current_elem) {
                    const auto& elements_sharing_face = topology.face_to_elements[face_id];
                    
                    // 只有内部面（被两个单元共享）才能连接邻居
                    if (elements_sharing_face.size() == 2) {
                        // 找到另一个共享该面的单元
                        ElementIndex neighbor_elem_idx = (elements_sharing_face[0] == current_elem_idx)
                                                       ? elements_sharing_face[1]
                                                       : elements_sharing_face[0];
                        
                        if (topology.element_to_body[neighbor_elem_idx] == -1) {
                            // 如果邻居未被访问，则标记并加入队列
                            topology.element_to_body[neighbor_elem_idx] = current_body_id;
                            q.push(neighbor_elem_idx);
                        }
                    }
                }
            }
            current_body_id++; // 为下一个发现的连续体准备新的ID
        }
    }
}

// -------------------------------------------------------------------
// **辅助函数: 从单元提取面 (根据单元库定义)**
// -------------------------------------------------------------------
std::vector<FaceKey> TopologySystems::get_faces_from_element(
    const std::vector<NodeID>& nodes, int element_type) {
    
    // 对于梁单元，它们是1D实体，没有“面”的概念，因此返回空列表。
    // 它们的连接性需要通过其他方式（如节点共享）来定义，而不是面共享。
    if (element_type == 102 || element_type == 103) {
        return {};
    }

    switch (element_type) {
        // --- 2D 单元 (面是边) ---
        case 203: // 3节点平面三角形
            if (nodes.size() == 3) {
                return { {nodes[0], nodes[1]}, {nodes[1], nodes[2]}, {nodes[2], nodes[0]} };
            }
            break;
        case 204: // 4节点平面四边形
            if (nodes.size() == 4) {
                return { {nodes[0], nodes[1]}, {nodes[1], nodes[2]}, {nodes[2], nodes[3]}, {nodes[3], nodes[0]} };
            }
            break;
        case 208: // 8节点二阶平面四边形
            // **关键**: 只使用角点节点 (前4个) 来定义拓扑边界。
            if (nodes.size() == 8) {
                return { {nodes[0], nodes[1]}, {nodes[1], nodes[2]}, {nodes[2], nodes[3]}, {nodes[3], nodes[0]} };
            }
            break;

        // --- 3D 单元 (面是2D多边形) ---
        case 304: // 4节点四面体
            if (nodes.size() == 4) {
                return {
                    {nodes[0], nodes[1], nodes[2]},
                    {nodes[0], nodes[3], nodes[1]},
                    {nodes[1], nodes[3], nodes[2]},
                    {nodes[2], nodes[3], nodes[0]}
                };
            }
            break;
        case 306: // 6节点楔形单元 (Penta)
            if (nodes.size() == 6) {
                return {
                    {nodes[0], nodes[1], nodes[2]},          // 底面三角形
                    {nodes[3], nodes[4], nodes[5]},          // 顶面三角形
                    {nodes[0], nodes[1], nodes[4], nodes[3]},  // 侧面四边形 1
                    {nodes[1], nodes[2], nodes[5], nodes[4]},  // 侧面四边形 2
                    {nodes[2], nodes[0], nodes[3], nodes[5]}   // 侧面四边形 3
                };
            }
            break;
        case 308: // 8节点六面体
            if (nodes.size() == 8) {
                return {
                    {nodes[0], nodes[1], nodes[2], nodes[3]}, // 底面
                    {nodes[4], nodes[5], nodes[6], nodes[7]}, // 顶面
                    {nodes[0], nodes[1], nodes[5], nodes[4]}, // 前面
                    {nodes[3], nodes[2], nodes[6], nodes[7]}, // 后面
                    {nodes[0], nodes[3], nodes[7], nodes[4]}, // 左面
                    {nodes[1], nodes[2], nodes[6], nodes[5]}  // 右面
                };
            }
            break;
        case 310: // 10节点二阶四面体
            // **关键**: 只使用角点节点 (前4个) 来定义拓扑面。
            if (nodes.size() == 10) {
                 return {
                    {nodes[0], nodes[1], nodes[2]},
                    {nodes[0], nodes[3], nodes[1]},
                    {nodes[1], nodes[3], nodes[2]},
                    {nodes[2], nodes[3], nodes[0]}
                };
            }
            break;
        case 320: // 20节点二阶六面体
            // **关键**: 只使用角点节点 (前8个) 来定义拓扑面。
            if (nodes.size() == 20) {
                return {
                    {nodes[0], nodes[1], nodes[2], nodes[3]}, // 底面
                    {nodes[4], nodes[5], nodes[6], nodes[7]}, // 顶面
                    {nodes[0], nodes[1], nodes[5], nodes[4]}, // 前面
                    {nodes[3], nodes[2], nodes[6], nodes[7]}, // 后面
                    {nodes[0], nodes[3], nodes[7], nodes[4]}, // 左面
                    {nodes[1], nodes[2], nodes[6], nodes[5]}  // 右面
                };
            }
            break;

        default:
            // 对于未知的单元类型，打印警告或直接返回空列表
            spdlog::warn("Warning: Unknown element type encountered: {}", element_type);
            return {};
    }

    // 如果因为节点数量不匹配而没有进入if语句，则返回空列表
    spdlog::warn("Warning: Node count mismatch for element type {}", element_type);
    return {};
}