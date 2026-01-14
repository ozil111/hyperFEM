// MassSystem.cpp
#include "MassSystem.h"
#include "../../data_center/components/mesh_components.h"
#include "../../data_center/components/property_components.h"
#include "c3d8/C3D8Mass.h"
#include "spdlog/spdlog.h"

void MassSystem::compute_lumped_mass(entt::registry& registry) {
    spdlog::info("Computing lumped mass matrix...");

    // Initialize mass to zero for all nodes
    auto node_view = registry.view<Component::Position>();
    for (auto node_entity : node_view) {
        if (!registry.all_of<Component::Mass>(node_entity)) {
            registry.emplace<Component::Mass>(node_entity, 0.0);
        } else {
            registry.get<Component::Mass>(node_entity).value = 0.0;
        }
    }

    // Traverse all elements
    auto element_view = registry.view<Component::Connectivity, Component::ElementType>();
    size_t element_count = 0;
    
    for (auto element_entity : element_view) {
        const auto& element_type = registry.get<Component::ElementType>(element_entity);
        
        // Switch-case dispatch based on element type
        switch (element_type.type_id) {
            case 308: {  // C3D8 (8-node hexahedron)
                // Get integration points from SolidProperty
                int n_integration_points = 1;  // Default to reduced integration
                
                if (registry.all_of<Component::PropertyRef>(element_entity)) {
                    const auto& property_ref = registry.get<Component::PropertyRef>(element_entity);
                    entt::entity property_entity = property_ref.property_entity;
                    
                    if (registry.all_of<Component::SolidProperty>(property_entity)) {
                        const auto& solid_prop = registry.get<Component::SolidProperty>(property_entity);
                        n_integration_points = solid_prop.integration_network;
                    } else {
                        spdlog::warn("Property missing SolidProperty component. Using default integration points = 1.");
                    }
                } else {
                    spdlog::warn("Element missing PropertyRef component. Using default integration points = 1.");
                }
                
                if (compute_c3d8_mass(registry, element_entity, n_integration_points)) {
                    element_count++;
                }
                break;
            }
            
            // Future element types can be added here:
            // case 304: {  // Tetra4
            //     if (compute_tet4_mass(registry, element_entity)) {
            //         element_count++;
            //     }
            //     break;
            // }
            
            default:
                // Skip unsupported element types
                break;
        }
    }

    spdlog::info("Lumped mass computed for {} elements.", element_count);
}
