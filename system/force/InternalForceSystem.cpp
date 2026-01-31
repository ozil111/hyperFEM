// InternalForceSystem.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "InternalForceSystem.h"
#include "../../data_center/components/mesh_components.h"
#include "../../data_center/components/property_components.h"
#include "c3d8r/C3D8RInternalForce.h"
#include "spdlog/spdlog.h"

void InternalForceSystem::reset_internal_forces(entt::registry& registry) {
    auto node_view = registry.view<Component::InternalForce>();
    for (auto node_entity : node_view) {
        auto& internal_force = registry.get<Component::InternalForce>(node_entity);
        internal_force.fx = 0.0;
        internal_force.fy = 0.0;
        internal_force.fz = 0.0;
    }
}

void InternalForceSystem::compute_internal_forces(entt::registry& registry) {
    // Reset internal forces first
    reset_internal_forces(registry);

    // Traverse all elements
    auto element_view = registry.view<Component::Connectivity, Component::ElementType>();
    size_t element_count = 0;

    for (auto element_entity : element_view) {
        const auto& element_type = registry.get<Component::ElementType>(element_entity);

        // switch-case dispatch based on element type
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
                
                // Only call C3D8R (reduced integration) if integration points = 1
                if (n_integration_points == 1) {
                    if (compute_c3d8r_internal_forces(registry, element_entity)) {
                        element_count++;
                    }
                } else {
                    // TODO: Implement full integration internal force calculation for n_integration_points > 1
                    spdlog::warn("Internal force calculation with {} integration points is not yet implemented. Skipping element.", n_integration_points);
                }
                break;
            }

            // Future element types can be added here:
            // case 304: {  // Tetra4
            //     if (compute_tet4_internal_forces(registry, element_entity)) {
            //         element_count++;
            //     }
            //     break;
            // }

            default:
                break;
        }
    }

    (void)element_count; // reserved for future logging/statistics
}
