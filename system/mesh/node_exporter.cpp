// system/mesh/node_exporter.cpp
#include "mesh/node_exporter.h"
#include "components/mesh_components.h"
#include "spdlog/spdlog.h"

namespace NodeExporter {

void save(std::ofstream& file, const entt::registry& registry) {
    spdlog::debug("--> Entering NodeExporter...");
    file << "*node begin\n";

    // Create a view of all entities with Position and OriginalID components
    auto view = registry.view<const Component::Position, const Component::OriginalID>();
    
    for (auto entity : view) {
        const auto& pos = view.get<const Component::Position>(entity);
        const auto& id = view.get<const Component::OriginalID>(entity);
        
        file << id.value << ", " << pos.x << ", " << pos.y << ", " << pos.z << "\n";
    }

    file << "*node end\n\n"; // Add extra newline for readability
    spdlog::debug("<-- Exiting NodeExporter. Exported {} nodes.", view.size_hint());
}

} // namespace NodeExporter
