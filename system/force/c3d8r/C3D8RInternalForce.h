// C3D8RInternalForce.h
#pragma once

#include "entt/entt.hpp"

/**
 * @brief Compute and scatter internal forces for a single C3D8R element
 * @param registry EnTT registry containing element and node data
 * @param element_entity Element entity to process
 * @return true if computed successfully, false otherwise
 * @details
 *   - Reads current Position and optional InitialPosition to build element displacement
 *   - Reads material D matrix from LinearElasticMatrix (via PropertyRef -> MaterialRef)
 *   - Computes B-bar at current configuration (reduced integration)
 *   - Computes element internal force vector and accumulates into node InternalForce
 */
bool compute_c3d8r_internal_forces(entt::registry& registry, entt::entity element_entity);

