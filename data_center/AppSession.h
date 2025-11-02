// Session.h
#pragma once
#include "mesh.h"
#include "TopologyData.h" // 引入我们之前设计的拓扑数据结构

struct AppSession {
    bool is_running = true;
    bool mesh_loaded = false;
    bool topology_built = false;

    Mesh mesh;
    std::unique_ptr<TopologyData> topology; // 使用指针，因为只有在需要时才创建

    AppSession() : topology(nullptr) {}

    void clear_mesh() {
        mesh.clear();
        topology.reset(); // 如果清除了网格，拓扑数据也必须失效
        mesh_loaded = false;
        topology_built = false;
    }
};