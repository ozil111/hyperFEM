// system/mesh/set_exporter.cpp
#include "mesh/set_exporter.h"
#include "components/mesh_components.h"
#include "spdlog/spdlog.h"

namespace SetExporter {

void save_node_sets(std::ofstream& file, const entt::registry& registry) {
    spdlog::debug("--> Entering NodeSetExporter...");
    file << "*nodeset begin\n";

    // Create a view of all node set entities
    auto view = registry.view<const Component::SetName, const Component::NodeSetMembers>();
    
    int set_id = 0; // Simple counter for set IDs in the output file
    for (auto set_entity : view) {
        const auto& set_name = view.get<const Component::SetName>(set_entity);
        const auto& members = view.get<const Component::NodeSetMembers>(set_entity);
        
        if (members.members.empty()) continue; // Don't write empty sets
        
        // 新格式: set_id, set_name, [member_ids]
        file << set_id << ", " << set_name.value << ", [";
        
        // Write member node IDs inside brackets
        for (size_t i = 0; i < members.members.size(); ++i) {
            const auto& node_id = registry.get<Component::OriginalID>(members.members[i]);
            file << node_id.value;
            if (i < members.members.size() - 1) {
                file << ", ";
            }
        }
        
        file << "]\n";
        
        ++set_id;
    }

    file << "*nodeset end\n\n";
    spdlog::debug("<-- Exiting NodeSetExporter. Exported {} node sets.", view.size_hint());
}

void save_element_sets(std::ofstream& file, const entt::registry& registry) {
    spdlog::debug("--> Entering ElementSetExporter...");
    file << "*eleset begin\n";

    // Create a view of all element set entities
    auto view = registry.view<const Component::SetName, const Component::ElementSetMembers>();
    
    int set_id = 0; // Simple counter for set IDs in the output file
    for (auto set_entity : view) {
        const auto& set_name = view.get<const Component::SetName>(set_entity);
        const auto& members = view.get<const Component::ElementSetMembers>(set_entity);
        
        if (members.members.empty()) continue; // Don't write empty sets
        
        // 新格式: set_id, set_name, [member_ids]
        file << set_id << ", " << set_name.value << ", [";
        
        // Write member element IDs inside brackets
        for (size_t i = 0; i < members.members.size(); ++i) {
            const auto& elem_id = registry.get<Component::OriginalID>(members.members[i]);
            file << elem_id.value;
            if (i < members.members.size() - 1) {
                file << ", ";
            }
        }
        
        file << "]\n";
        
        ++set_id;
    }

    file << "*eleset end\n\n";
    spdlog::debug("<-- Exiting ElementSetExporter. Exported {} element sets.", view.size_hint());
}

} // namespace SetExporter
