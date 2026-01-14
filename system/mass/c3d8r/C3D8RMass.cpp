// C3D8RMass.cpp
#include "C3D8RMass.h"
#include "../c3d8/C3D8Mass.h"

bool compute_c3d8r_mass(entt::registry& registry, entt::entity element_entity) {
    // C3D8R uses reduced integration (1 integration point per dimension)
    return compute_c3d8_mass(registry, element_entity, 1);
}
