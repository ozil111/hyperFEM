// C3D8RMass.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "C3D8RMass.h"
#include "../c3d8/C3D8Mass.h"

bool compute_c3d8r_mass(entt::registry& registry, entt::entity element_entity) {
    // C3D8R uses reduced integration (1 integration point per dimension)
    return compute_c3d8_mass(registry, element_entity, 1);
}
