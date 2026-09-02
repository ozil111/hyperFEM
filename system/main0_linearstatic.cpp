// main0_linearstatic.cpp
// Extracted linear static solver logic from main.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */

#include <spdlog/spdlog.h>
#include "DataContext.h"
#include "assemble/AssemblySystem.h"
#include "components/mesh_components.h"
#include "components/analysis_component.h"
#include "components/load_components.h"
#include "dof/DofNumberingSystem.h"
#include "load/LoadSystem.h"
#include "material/dmatMain.h"
#include "material/mat1/LinearElasticMatrixSystem.h"
#include "material/dmatMain.h"
#include "components/simdroid_components.h"
#include "mesh/TopologySystems.h"
#include "output/VtkExporter.h"

#include <Eigen/SparseCholesky>
#include <Eigen/SparseLU>
#include <array>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct ConstraintInfo {
    std::vector<char> is_constrained;  // 0/1 flags, size = ndof
    Eigen::VectorXd u_prescribed;      // prescribed displacement, size = ndof
};

static inline void mark_dofs_from_string(
    const std::string& dof_str_raw,
    std::array<char, 3>& mask_xyz
) {
    mask_xyz = {0, 0, 0};
    std::string dof = dof_str_raw;
    for (auto& c : dof) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (dof == "all" || dof == "xyz") {
        mask_xyz = {1, 1, 1};
    } else if (dof == "x") {
        mask_xyz = {1, 0, 0};
    } else if (dof == "y") {
        mask_xyz = {0, 1, 0};
    } else if (dof == "z") {
        mask_xyz = {0, 0, 1};
    } else if (dof == "xy" || dof == "yx") {
        mask_xyz = {1, 1, 0};
    } else if (dof == "xz" || dof == "zx") {
        mask_xyz = {1, 0, 1};
    } else if (dof == "yz" || dof == "zy") {
        mask_xyz = {0, 1, 1};
    }
}

static ConstraintInfo build_constraints(entt::registry& registry, int ndof) {
    ConstraintInfo info;
    info.is_constrained.assign(static_cast<size_t>(ndof), 0);
    info.u_prescribed = Eigen::VectorXd::Zero(ndof);

    if (!registry.ctx().contains<DofMap>()) {
        spdlog::error("LinearStatic: DofMap not found in Context. Build DOF map first.");
        return info;
    }
    const auto& dof_map = registry.ctx().get<DofMap>();

    auto boundary_view = registry.view<Component::AppliedBoundaryRef>();
    for (auto node_entity : boundary_view) {
        const auto& boundary_ref = registry.get<Component::AppliedBoundaryRef>(node_entity);
        if (!dof_map.has_node(node_entity)) {
            continue;
        }

        for (const auto boundary_entity : boundary_ref.boundary_entities) {
            if (!registry.valid(boundary_entity) || !registry.all_of<Component::BoundarySPC>(boundary_entity)) {
                continue;
            }
            const auto& spc = registry.get<Component::BoundarySPC>(boundary_entity);
            std::array<char, 3> mask{};
            mark_dofs_from_string(spc.dof, mask);

            for (int d = 0; d < 3; ++d) {
                if (!mask[static_cast<size_t>(d)]) continue;
                int gi = dof_map.get_dof_index(node_entity, d);
                if (gi >= 0 && gi < ndof) {
                    info.is_constrained[static_cast<size_t>(gi)] = 1;
                    info.u_prescribed[gi] = spc.value;
                }
            }
        }
    }

    return info;
}

static Eigen::VectorXd build_force_vector(entt::registry& registry, int ndof) {
    Eigen::VectorXd F = Eigen::VectorXd::Zero(ndof);
    if (!registry.ctx().contains<DofMap>()) {
        spdlog::error("LinearStatic: DofMap not found in Context. Build DOF map first.");
        return F;
    }
    const auto& dof_map = registry.ctx().get<DofMap>();

    auto node_view = registry.view<Component::Position>();
    for (auto node_entity : node_view) {
        if (!dof_map.has_node(node_entity)) continue;
        if (!registry.all_of<Component::ExternalForce>(node_entity)) continue;

        const auto& f = registry.get<Component::ExternalForce>(node_entity);
        int ix = dof_map.get_dof_index(node_entity, 0);
        int iy = dof_map.get_dof_index(node_entity, 1);
        int iz = dof_map.get_dof_index(node_entity, 2);
        if (ix >= 0 && ix < ndof) F[ix] += f.fx;
        if (iy >= 0 && iy < ndof) F[iy] += f.fy;
        if (iz >= 0 && iz < ndof) F[iz] += f.fz;
    }
    return F;
}
}  // namespace

void run_linearstatic_solver(DataContext& data_context) {
    spdlog::info("Starting linear static solver...");

    auto& registry = data_context.registry;

    // 1) Material D matrices per part
    spdlog::info("Computing material D matrices per part...");
    {
        auto part_view = registry.view<Component::SimdroidPart>();
        size_t part_count = 0;
        for (auto part_entity : part_view) {
            const auto& part = registry.get<Component::SimdroidPart>(part_entity);
            spdlog::info("  Part '{}': computing D matrix for material...", part.name);

            auto material_entity = part.material;
            dmat_main(registry, material_entity);

            part_count++;
        }
        spdlog::info("Computed D matrices for {} part(s).", part_count);
    }

    // 2) DOF map
    spdlog::info("Building DOF map...");
    DofNumberingSystem::build_dof_map(registry);
    if (!registry.ctx().contains<DofMap>()) {
        spdlog::error("LinearStatic: Failed to build DofMap.");
        return;
    }
    const auto& dof_map = registry.ctx().get<DofMap>();
    const int ndof = dof_map.num_total_dofs;
    if (ndof <= 0) {
        spdlog::warn("LinearStatic: Zero DOFs. Nothing to solve.");
        return;
    }

    // 3) Apply loads at end-time (static)
    double load_time = 1.0;  // default end time
    {
        auto analysis_view = registry.view<Component::AnalysisType, Component::EndTime>();
        for (auto e : analysis_view) {
            load_time = registry.get<Component::EndTime>(e).value;
            break;
        }
    }
    spdlog::info("Applying loads (t={})...", load_time);
    LoadSystem::reset_external_forces(registry);
    LoadSystem::apply_nodal_loads(registry, load_time);
    Eigen::VectorXd F = build_force_vector(registry, ndof);

    // 4) Assemble global stiffness K
    spdlog::info("Assembling global stiffness matrix...");
    AssemblySystem::SparseMatrix K;
    
    // AssemblySystem needs TopologyData for (element -> Part -> material) mapping.
    if (!registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
        spdlog::info("TopologyData not found. Building topology before assembly...");
        TopologySystems::extract_topology(registry);
    }
    AssemblySystem::assemble_stiffness(registry, K);
    if (K.rows() != ndof || K.cols() != ndof) {
        spdlog::error("LinearStatic: Stiffness matrix size mismatch. K is {}x{}, expected {}x{}.",
                      K.rows(), K.cols(), ndof, ndof);
        return;
    }

    // 5) Constraints (SPC) and reduction
    spdlog::info("Collecting SPC constraints...");
    const ConstraintInfo cons = build_constraints(registry, ndof);

    std::vector<int> old_to_free(ndof, -1);
    std::vector<int> free_to_old;
    free_to_old.reserve(static_cast<size_t>(ndof));
    for (int i = 0; i < ndof; ++i) {
        if (!cons.is_constrained[static_cast<size_t>(i)]) {
            old_to_free[i] = static_cast<int>(free_to_old.size());
            free_to_old.push_back(i);
        }
    }
    const int nfree = static_cast<int>(free_to_old.size());
    if (nfree == 0) {
        spdlog::warn("LinearStatic: All DOFs are constrained. Skipping solve.");
    }

    // Build reduced system K_ff and adjusted RHS: F_f - K_fc * u_c
    spdlog::info("Building reduced system (free DOFs = {})...", nfree);
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(static_cast<size_t>(K.nonZeros()));
    Eigen::VectorXd rhs = Eigen::VectorXd::Zero(nfree);

    // Initialize rhs from F on free dofs
    for (int fi = 0; fi < nfree; ++fi) {
        rhs[fi] = F[free_to_old[fi]];
    }

    // Row-major: efficient row iteration
    for (int i = 0; i < K.outerSize(); ++i) {
        const int fi = old_to_free[i];
        if (fi < 0) {
            continue; // constrained row does not appear in reduced system
        }
        for (AssemblySystem::SparseMatrix::InnerIterator it(K, i); it; ++it) {
            const int j = it.col();
            const double v = it.value();
            const int fj = old_to_free[j];
            if (fj >= 0) {
                triplets.emplace_back(fi, fj, v);
            } else if (cons.is_constrained[static_cast<size_t>(j)]) {
                rhs[fi] -= v * cons.u_prescribed[j];
            }
        }
    }

    AssemblySystem::SparseMatrix Kff(nfree, nfree);
    Kff.setFromTriplets(triplets.begin(), triplets.end());
    Kff.makeCompressed();

    // 6) Solve
    Eigen::VectorXd u = cons.u_prescribed;  // full solution, start with prescribed values
    if (nfree > 0) {
        spdlog::info("Solving linear system...");

        Eigen::SimplicialLDLT<AssemblySystem::SparseMatrix> ldlt;
        ldlt.compute(Kff);

        Eigen::VectorXd uf;
        bool solved = false;
        if (ldlt.info() == Eigen::Success) {
            uf = ldlt.solve(rhs);
            solved = (ldlt.info() == Eigen::Success);
        }

        if (!solved) {
            spdlog::warn("SimplicialLDLT failed. Falling back to SparseLU...");
            Eigen::SparseLU<AssemblySystem::SparseMatrix> lu;
            lu.analyzePattern(Kff);
            lu.factorize(Kff);
            if (lu.info() == Eigen::Success) {
                uf = lu.solve(rhs);
                solved = (lu.info() == Eigen::Success);
            }
        }

        if (!solved) {
            spdlog::error("LinearStatic: Failed to solve the linear system.");
            return;
        }

        for (int fi = 0; fi < nfree; ++fi) {
            u[free_to_old[fi]] = uf[fi];
        }
    }

    // 7) Write back displacement and update positions for output
    spdlog::info("Writing displacements back to registry...");
    auto node_view = registry.view<Component::Position>();
    for (auto node_entity : node_view) {
        if (!dof_map.has_node(node_entity)) continue;

        auto& pos = registry.get<Component::Position>(node_entity);
        if (!registry.all_of<Component::InitialPosition>(node_entity)) {
            registry.emplace<Component::InitialPosition>(node_entity, pos.x, pos.y, pos.z);
        }
        const auto& x0 = registry.get<Component::InitialPosition>(node_entity);

        const int ix = dof_map.get_dof_index(node_entity, 0);
        const int iy = dof_map.get_dof_index(node_entity, 1);
        const int iz = dof_map.get_dof_index(node_entity, 2);

        const double dx = (ix >= 0 && ix < ndof) ? u[ix] : 0.0;
        const double dy = (iy >= 0 && iy < ndof) ? u[iy] : 0.0;
        const double dz = (iz >= 0 && iz < ndof) ? u[iz] : 0.0;

        if (!registry.all_of<Component::Displacement>(node_entity)) {
            registry.emplace<Component::Displacement>(node_entity, dx, dy, dz);
        } else {
            auto& disp = registry.get<Component::Displacement>(node_entity);
            disp.dx = dx;
            disp.dy = dy;
            disp.dz = dz;
        }

        // Update current positions (for output pipelines that read Position)
        pos.x = x0.x0 + dx;
        pos.y = x0.y0 + dy;
        pos.z = x0.z0 + dz;
    }

    // 8) Optional output (single snapshot)
    const bool has_cli_output = !data_context.cli_output_path.empty();
    const bool do_output = (!has_cli_output &&
                            data_context.output_entity != entt::null &&
                            registry.valid(data_context.output_entity));
    if (do_output) {
        std::filesystem::create_directories("result");
        std::ostringstream oss;
        oss << "result/static_" << std::setfill('0') << std::setw(4) << 0 << ".vtk";
        VtkExporter::save(oss.str(), data_context, data_context.output_entity);
        spdlog::info("LinearStatic: Wrote output to {}", oss.str());
    }

    spdlog::info("Linear static solver completed.");
}

