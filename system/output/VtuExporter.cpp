// system/output/VtuExporter.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "output/VtuExporter.h"
#include "DataContext.h"
#include "components/mesh_components.h"
#include "components/analysis_component.h"
#include <tinyxml2.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace {

/** 内部 type_id -> VTK cell type。参见 docs/vtu_format.md（八节点六面体 = 12）。*/
int toVtkCellType(int type_id) {
    switch (type_id) {
        case 304: return 10;   // Tetra4  -> VTK_TETRA
        case 306: return 13;   // Penta6  -> VTK_WEDGE
        case 308: return 12;   // Hexa8   -> VTK_HEXAHEDRON
        case 310: return 24;   // Tetra10 -> VTK_QUADRATIC_TETRA
        case 320: return 25;   // Hexa20  -> VTK_QUADRATIC_HEXAHEDRON
        default:  return 12;   // 未知时按 Hexa8 处理，避免写坏文件
    }
}

inline bool wantField(const std::vector<std::string>* list, const char* name) {
    if (!list || list->empty()) return true;
    return std::find(list->begin(), list->end(), std::string(name)) != list->end();
}

} // namespace

bool VtuExporter::save(const std::string& filepath, const DataContext& data_context, entt::entity output_entity) {
    using namespace tinyxml2;
    const auto& registry = data_context.registry;

    auto pos_view = registry.view<Component::Position>();
    auto cell_view = registry.view<Component::Connectivity, Component::ElementType>();

    const size_t num_points = pos_view.size();
    const size_t num_cells  = cell_view.size_hint();
    if (num_points == 0 || num_cells == 0) {
        spdlog::warn("VtuExporter: no points or no cells, skip.");
        return false;
    }

    // 稳定顺序：先收集所有节点实体，再建 entity -> 点下标
    std::vector<entt::entity> node_entities;
    node_entities.reserve(num_points);
    for (auto e : pos_view)
        node_entities.push_back(e);

    std::unordered_map<entt::entity, size_t> entity_to_index;
    entity_to_index.reserve(node_entities.size());
    for (size_t i = 0; i < node_entities.size(); ++i)
        entity_to_index[node_entities[i]] = i;

    XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());
    XMLElement* vtkFile = doc.NewElement("VTKFile");
    vtkFile->SetAttribute("type", "UnstructuredGrid");
    vtkFile->SetAttribute("version", "1.0");
    vtkFile->SetAttribute("byte_order", "LittleEndian");
    doc.InsertEndChild(vtkFile);

    XMLElement* grid = doc.NewElement("UnstructuredGrid");
    vtkFile->InsertEndChild(grid);

    XMLElement* piece = doc.NewElement("Piece");
    piece->SetAttribute("NumberOfPoints", static_cast<unsigned int>(num_points));
    piece->SetAttribute("NumberOfCells",  static_cast<unsigned int>(num_cells));
    grid->InsertEndChild(piece);

    // --- Points (Float64, 3 components) ---
    {
        std::ostringstream os;
        for (entt::entity e : node_entities) {
            const auto& p = registry.get<Component::Position>(e);
            os << p.x << ' ' << p.y << ' ' << p.z << ' ';
        }
        XMLElement* points = doc.NewElement("Points");
        piece->InsertEndChild(points);
        XMLElement* da = doc.NewElement("DataArray");
        da->SetAttribute("type", "Float64");
        da->SetAttribute("NumberOfComponents", 3);
        da->SetAttribute("format", "ascii");
        da->SetText(os.str().c_str());
        points->InsertEndChild(da);
    }

    // --- Cells: connectivity, offsets, types ---
    {
        std::ostringstream conn_os, off_os, type_os;
        int offset = 0;
        for (auto cell_entity : cell_view) {
            const auto& conn = cell_view.get<Component::Connectivity>(cell_entity);
            const auto& etype = cell_view.get<Component::ElementType>(cell_entity);
            for (entt::entity node_entity : conn.nodes) {
                auto it = entity_to_index.find(node_entity);
                if (it != entity_to_index.end())
                    conn_os << it->second << ' ';
            }
            offset += static_cast<int>(conn.nodes.size());
            off_os << offset << ' ';
            type_os << toVtkCellType(etype.type_id) << ' ';
        }

        XMLElement* cells = doc.NewElement("Cells");
        piece->InsertEndChild(cells);

        XMLElement* conn_da = doc.NewElement("DataArray");
        conn_da->SetAttribute("type", "Int32");
        conn_da->SetAttribute("Name", "connectivity");
        conn_da->SetAttribute("format", "ascii");
        conn_da->SetText(conn_os.str().c_str());
        cells->InsertEndChild(conn_da);

        XMLElement* off_da = doc.NewElement("DataArray");
        off_da->SetAttribute("type", "Int32");
        off_da->SetAttribute("Name", "offsets");
        off_da->SetAttribute("format", "ascii");
        off_da->SetText(off_os.str().c_str());
        cells->InsertEndChild(off_da);

        XMLElement* type_da = doc.NewElement("DataArray");
        type_da->SetAttribute("type", "UInt8");
        type_da->SetAttribute("Name", "types");
        type_da->SetAttribute("format", "ascii");
        type_da->SetText(type_os.str().c_str());
        cells->InsertEndChild(type_da);
    }

    const std::vector<std::string>* node_fields = nullptr;
    const std::vector<std::string>* elem_fields = nullptr;
    if (output_entity != entt::null && registry.valid(output_entity)) {
        if (registry.all_of<Component::NodeOutput>(output_entity))
            node_fields = &registry.get<Component::NodeOutput>(output_entity).node_output;
        if (registry.all_of<Component::ElementOutput>(output_entity))
            elem_fields = &registry.get<Component::ElementOutput>(output_entity).element_output;
    }

    // --- PointData: 仅写出 output 指定的节点场（无指定时默认写 Displacement）---
    {
        XMLElement* pointData = doc.NewElement("PointData");
        piece->InsertEndChild(pointData);
        if (wantField(node_fields, "Displacement")) {
            std::ostringstream os;
            for (entt::entity e : node_entities) {
                if (registry.all_of<Component::Displacement>(e)) {
                    const auto& d = registry.get<Component::Displacement>(e);
                    os << d.dx << ' ' << d.dy << ' ' << d.dz << ' ';
                } else {
                    os << "0 0 0 ";
                }
            }
            XMLElement* da = doc.NewElement("DataArray");
            da->SetAttribute("type", "Float64");
            da->SetAttribute("Name", "Displacement");
            da->SetAttribute("NumberOfComponents", 3);
            da->SetAttribute("format", "ascii");
            da->SetText(os.str().c_str());
            pointData->InsertEndChild(da);
        }
        if (wantField(node_fields, "Velocity")) {
            std::ostringstream os;
            for (entt::entity e : node_entities) {
                if (registry.all_of<Component::Velocity>(e)) {
                    const auto& v = registry.get<Component::Velocity>(e);
                    os << v.vx << ' ' << v.vy << ' ' << v.vz << ' ';
                } else {
                    os << "0 0 0 ";
                }
            }
            XMLElement* da = doc.NewElement("DataArray");
            da->SetAttribute("type", "Float64");
            da->SetAttribute("Name", "Velocity");
            da->SetAttribute("NumberOfComponents", 3);
            da->SetAttribute("format", "ascii");
            da->SetText(os.str().c_str());
            pointData->InsertEndChild(da);
        }
        if (wantField(node_fields, "Acceleration")) {
            std::ostringstream os;
            for (entt::entity e : node_entities) {
                if (registry.all_of<Component::Acceleration>(e)) {
                    const auto& a = registry.get<Component::Acceleration>(e);
                    os << a.ax << ' ' << a.ay << ' ' << a.az << ' ';
                } else {
                    os << "0 0 0 ";
                }
            }
            XMLElement* da = doc.NewElement("DataArray");
            da->SetAttribute("type", "Float64");
            da->SetAttribute("Name", "Acceleration");
            da->SetAttribute("NumberOfComponents", 3);
            da->SetAttribute("format", "ascii");
            da->SetText(os.str().c_str());
            pointData->InsertEndChild(da);
        }
        // TODO: Reaction Force (PointData) — 需在 data_center 提供对应组件后再写入
    }

    // --- CellData: 仅写出 output 指定的单元场；Stress/Strain/Mises 等尚待组件支持 ---
    {
        XMLElement* cellData = doc.NewElement("CellData");
        piece->InsertEndChild(cellData);
        if (elem_fields) {
            for (const std::string& name : *elem_fields) {
                if (name == "Stress" || name == "Strain" || name == "Mises" || name == "Equivalent") {
                    // TODO: 需在 data_center 提供对应单元分量后再写入
                    (void)name;
                }
            }
        }
    }

    if (doc.SaveFile(filepath.c_str()) != XML_SUCCESS) {
        spdlog::error("VtuExporter could not write file: {}", filepath);
        return false;
    }
    spdlog::info("VtuExporter wrote: {}", filepath);
    return true;
}
