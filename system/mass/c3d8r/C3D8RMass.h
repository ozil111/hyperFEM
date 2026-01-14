// C3D8RMass.h
#pragma once

#include "entt/entt.hpp"

/**
 * @brief Compute lumped mass for C3D8R (8-node hexahedron) elements
 * @param registry EnTT registry containing elements and nodes
 * @param element_entity Element entity to process
 * @return true if mass was successfully computed and distributed, false otherwise
 * @details
 *   - Gets material density from LinearElasticParams
 *   - Computes element volume using B-bar method
 *   - Distributes element mass (rho * V) uniformly to 8 nodes
 *   - Accumulates mass in Component::Mass for each node
 */
bool compute_c3d8r_mass(entt::registry& registry, entt::entity element_entity);
