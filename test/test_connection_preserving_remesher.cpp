#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include <filesystem>
#include "remesh/ConnectionPreservingRemesher.h"
#include "parser_simdroid/SimdroidParser.h"
#include "DataContext.h"
#include "components/load_components.h"
#include "components/material_components.h"
#include "components/mesh_components.h"
#include "components/property_components.h"
#include "components/simdroid_components.h"

namespace {

entt::entity create_node(entt::registry& registry, int id, double x, double y, double z) {
    auto node = registry.create();
    registry.emplace<Component::NodeID>(node, id);
    registry.emplace<Component::Position>(node, x, y, z);
    return node;
}

entt::entity create_tet(entt::registry& registry,
                        int id,
                        const std::vector<entt::entity>& nodes,
                        entt::entity property) {
    auto element = registry.create();
    registry.emplace<Component::ElementID>(element, id);
    registry.emplace<Component::ElementType>(element, 304);
    registry.emplace<Component::PropertyRef>(element, property);
    registry.emplace<Component::Connectivity>(element, Component::Connectivity{nodes});
    return element;
}

entt::entity create_part(entt::registry& registry,
                         const std::string& name,
                         entt::entity element,
                         entt::entity material,
                         entt::entity property) {
    auto set = registry.create();
    registry.emplace<Component::SetName>(set, name + "_elements");
    registry.emplace<Component::ElementSetMembers>(set, std::vector<entt::entity>{element});

    auto part_entity = registry.create();
    Component::SimdroidPart part;
    part.name = name;
    part.element_set = set;
    part.material = material;
    part.section = property;
    registry.emplace<Component::SimdroidPart>(part_entity, std::move(part));
    return part_entity;
}

} // namespace

TEST(ConnectionPreservingRemesherTest, BuildsPlanWithSharedNodeInterface) {
    entt::registry registry;

    auto material = registry.create();
    registry.emplace<Component::MaterialID>(material, 1);
    registry.emplace<Component::MaterialModel>(material, "IsotropicElastic");

    auto property = registry.create();
    registry.emplace<Component::PropertyID>(property, 1);
    registry.emplace<Component::SolidProperty>(property, 304);
    registry.emplace<Component::IntegrationPoints>(property, 1);

    auto n1 = create_node(registry, 1, 0.0, 0.0, 0.0);
    auto n2 = create_node(registry, 2, 1.0, 0.0, 0.0);
    auto n3 = create_node(registry, 3, 0.0, 1.0, 0.0);
    auto n4 = create_node(registry, 4, 0.0, 0.0, 1.0);
    auto n5 = create_node(registry, 5, 1.0, 1.0, 0.0);
    auto n6 = create_node(registry, 6, 1.0, 0.0, 1.0);
    auto n7 = create_node(registry, 7, 0.0, 1.0, 1.0);

    auto e1 = create_tet(registry, 10, {n1, n2, n3, n4}, property);
    auto e2 = create_tet(registry, 20, {n4, n5, n6, n7}, property);

    create_part(registry, "Part_A", e1, material, property);
    create_part(registry, "Part_B", e2, material, property);

    SimdroidInspector inspector;
    inspector.build(registry);

    RemeshOptions options;
    options.target_compression_ratio = 100.0;
    RemeshPlan plan = ConnectionPreservingRemesher::build_plan(registry, inspector, options);

    EXPECT_EQ(plan.parts.size(), 2);
    EXPECT_EQ(plan.original_element_count, 2);
    EXPECT_EQ(plan.target_element_count, 2);
    ASSERT_EQ(plan.interfaces.size(), 1);
    EXPECT_EQ(plan.interfaces[0].source_part, "Part_A");
    EXPECT_EQ(plan.interfaces[0].target_part, "Part_B");
    EXPECT_EQ(plan.interfaces[0].type, ConnectionType::SharedNode);

    RemeshValidationResult ok =
        ConnectionPreservingRemesher::validate_preservation(plan, plan);
    EXPECT_TRUE(ok.valid);

    RemeshPlan broken = plan;
    broken.interfaces.clear();
    RemeshValidationResult bad =
        ConnectionPreservingRemesher::validate_preservation(plan, broken);
    EXPECT_FALSE(bad.valid);
    EXPECT_FALSE(bad.errors.empty());
}

TEST(ConnectionPreservingRemesherTest, DoublePartHex8CasePlansWithTieInterface) {
    const std::filesystem::path control_path =
        std::filesystem::path("case") / "double_part_hex8" / "doublepart_hex8_inp" / "control.json";
    const std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
    if (!std::filesystem::exists(control_path) || !std::filesystem::exists(mesh_path)) {
        GTEST_SKIP() << "double_part_hex8 Simdroid case is not available";
    }

    DataContext ctx;
    ASSERT_TRUE(SimdroidParser::parse(mesh_path.string(), control_path.string(), ctx))
        << "failed to parse double_part_hex8 case";

    SimdroidInspector inspector;
    inspector.build(ctx.registry);

    RemeshOptions options;
    options.target_compression_ratio = 100.0;
    RemeshPlan plan = ConnectionPreservingRemesher::build_plan(ctx.registry, inspector, options);

    EXPECT_EQ(plan.original_element_count, 40000);
    ASSERT_EQ(plan.parts.size(), 2);
    EXPECT_EQ(plan.parts[0].original_element_count, 20000);
    EXPECT_EQ(plan.parts[1].original_element_count, 20000);
    ASSERT_EQ(plan.parts[0].element_type_counts.size(), 1);
    EXPECT_EQ(plan.parts[0].element_type_counts.at(308), 20000);
    ASSERT_EQ(plan.parts[1].element_type_counts.size(), 1);
    EXPECT_EQ(plan.parts[1].element_type_counts.at(308), 20000);

    bool has_contact_interface = false;
    for (const auto& sig : plan.interfaces) {
        if (sig.type == ConnectionType::Contact) has_contact_interface = true;
    }
    EXPECT_TRUE(has_contact_interface);

    auto protected_infos =
        ConnectionPreservingRemesher::extract_protected_entities(ctx.registry, inspector);
    ASSERT_EQ(protected_infos.size(), 2u);
    int total_contact_nodes = 0;
    int total_loaded_nodes = 0;
    int total_constrained_nodes = 0;
    int total_shared_nodes = 0;
    for (const auto& info : protected_infos) {
        total_contact_nodes += static_cast<int>(info.contact_nodes.size());
        total_loaded_nodes += static_cast<int>(info.loaded_nodes.size());
        total_constrained_nodes += static_cast<int>(info.constrained_nodes.size());
        total_shared_nodes += static_cast<int>(info.shared_nodes.size());
    }
    EXPECT_GT(total_contact_nodes, 0);
    EXPECT_GT(total_loaded_nodes, 0);
    EXPECT_GT(total_constrained_nodes, 0);
    EXPECT_EQ(total_shared_nodes, 0);
}

TEST(ConnectionPreservingRemesherTest, BuildsPlanForCantileverBeamCase) {
    const std::filesystem::path control_path =
        std::filesystem::path("case") / "cantilever beam" / "cantilever_beam_inp" / "control.json";
    const std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
    if (!std::filesystem::exists(control_path) || !std::filesystem::exists(mesh_path)) {
        GTEST_SKIP() << "cantilever beam Simdroid case is not available";
    }

    DataContext ctx;
    ASSERT_TRUE(SimdroidParser::parse(mesh_path.string(), control_path.string(), ctx));

    SimdroidInspector inspector;
    inspector.build(ctx.registry);

    RemeshOptions options;
    options.target_compression_ratio = 100.0;
    RemeshPlan plan = ConnectionPreservingRemesher::build_plan(ctx.registry, inspector, options);

    EXPECT_EQ(plan.original_element_count, 20000);
    EXPECT_EQ(plan.target_element_count, 200);
    ASSERT_EQ(plan.parts.size(), 1);
    EXPECT_EQ(plan.parts[0].part_name, "Component_1_Set-1");
    EXPECT_EQ(plan.parts[0].original_element_count, 20000);
    EXPECT_EQ(plan.parts[0].target_element_count, 200);
    ASSERT_EQ(plan.parts[0].element_type_counts.size(), 1);
    EXPECT_EQ(plan.parts[0].element_type_counts.at(308), 20000);
    EXPECT_TRUE(plan.parts[0].has_load);
    EXPECT_TRUE(plan.parts[0].has_constraint);
    EXPECT_TRUE(plan.interfaces.empty());
}

TEST(ConnectionPreservingRemesherTest, StructuredHex8RemeshesDoublePartTieCase) {
    const std::filesystem::path control_path =
        std::filesystem::path("case") / "double_part_hex8" / "doublepart_hex8_inp" / "control.json";
    const std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
    if (!std::filesystem::exists(control_path) || !std::filesystem::exists(mesh_path)) {
        GTEST_SKIP() << "double_part_hex8 Simdroid case is not available";
    }

    DataContext ctx;
    ASSERT_TRUE(SimdroidParser::parse(mesh_path.string(), control_path.string(), ctx));

    SimdroidInspector inspector;
    RemeshOptions options;
    options.target_compression_ratio = 100.0;

    RemeshExecutionResult result =
        ConnectionPreservingRemesher::remesh_structured_hex8(ctx, inspector, options);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.validation.valid);

    EXPECT_EQ(result.before.original_element_count, 40000);
    ASSERT_EQ(result.before.parts.size(), 2);
    ASSERT_EQ(result.after.parts.size(), 2);

    for (const auto& pp : result.after.parts) {
        ASSERT_EQ(pp.element_type_counts.size(), 1);
        EXPECT_EQ(pp.element_type_counts.at(308), pp.original_element_count);
        EXPECT_GT(pp.original_element_count, 0);
    }

    // The Tie contact interface must be preserved.
    ASSERT_FALSE(result.after.interfaces.empty());
    bool has_contact_after = false;
    for (const auto& sig : result.after.interfaces) {
        if (sig.type == ConnectionType::Contact) has_contact_after = true;
    }
    EXPECT_TRUE(has_contact_after);

    // Each part should still report load/constraint coverage.
    bool any_load = false;
    bool any_constraint = false;
    for (const auto& pp : result.after.parts) {
        if (pp.has_load) any_load = true;
        if (pp.has_constraint) any_constraint = true;
    }
    EXPECT_TRUE(any_load);
    EXPECT_TRUE(any_constraint);
}

TEST(ConnectionPreservingRemesherTest, StructuredHex8RemeshesCantileverBeamToTargetCount) {
    const std::filesystem::path control_path =
        std::filesystem::path("case") / "cantilever beam" / "cantilever_beam_inp" / "control.json";
    const std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
    if (!std::filesystem::exists(control_path) || !std::filesystem::exists(mesh_path)) {
        GTEST_SKIP() << "cantilever beam Simdroid case is not available";
    }

    DataContext ctx;
    ASSERT_TRUE(SimdroidParser::parse(mesh_path.string(), control_path.string(), ctx));

    SimdroidInspector inspector;
    RemeshOptions options;
    options.target_compression_ratio = 100.0;

    RemeshExecutionResult result =
        ConnectionPreservingRemesher::remesh_structured_hex8(ctx, inspector, options);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.validation.valid);
    EXPECT_EQ(result.before.original_element_count, 20000);
    EXPECT_EQ(result.before.target_element_count, 200);
    EXPECT_EQ(result.after.original_element_count, 200);
    ASSERT_EQ(result.after.parts.size(), 1);
    ASSERT_EQ(result.after.parts[0].element_type_counts.size(), 1);
    EXPECT_EQ(result.after.parts[0].element_type_counts.at(308), 200);
    EXPECT_TRUE(result.after.parts[0].has_load);
    EXPECT_TRUE(result.after.parts[0].has_constraint);
}

TEST(ConnectionPreservingRemesherTest, StructuredTet4RemeshesCantileverBeamTet4Case) {
    const std::filesystem::path control_path =
        std::filesystem::path("case") / "cantilever_beam_tet4" / "cantilever_beam_tet4_inp" / "control.json";
    const std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
    if (!std::filesystem::exists(control_path) || !std::filesystem::exists(mesh_path)) {
        GTEST_SKIP() << "cantilever_beam_tet4 Simdroid case is not available";
    }

    DataContext ctx;
    ASSERT_TRUE(SimdroidParser::parse(mesh_path.string(), control_path.string(), ctx))
        << "failed to parse cantilever_beam_tet4 case";

    SimdroidInspector inspector;
    inspector.build(ctx.registry);

    // Verify the source mesh is single-type Tet (304 or 310).
    RemeshOptions options;
    options.target_compression_ratio = 100.0;
    RemeshPlan plan = ConnectionPreservingRemesher::build_plan(ctx.registry, inspector, options);
    ASSERT_EQ(plan.parts.size(), 1);
    ASSERT_EQ(plan.parts[0].element_type_counts.size(), 1);
    const int source_type = plan.parts[0].element_type_counts.begin()->first;
    EXPECT_TRUE(source_type == 304 || source_type == 310) << "expected Tet4 or Tet10, got " << source_type;
    const int original_count = plan.parts[0].original_element_count;
    ASSERT_GT(original_count, 0);

    // Dispatch should route to the Tet strategy.
    RemeshExecutionResult result =
        ConnectionPreservingRemesher::remesh(ctx, inspector, options);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.validation.valid);

    // Element count must decrease.
    EXPECT_LT(result.after.original_element_count, result.before.original_element_count);

    // Element type must be preserved.
    ASSERT_EQ(result.after.parts.size(), 1);
    ASSERT_EQ(result.after.parts[0].element_type_counts.size(), 1);
    EXPECT_EQ(result.after.parts[0].element_type_counts.begin()->first, source_type);

    // Load and constraint coverage must be preserved.
    EXPECT_TRUE(result.after.parts[0].has_load);
    EXPECT_TRUE(result.after.parts[0].has_constraint);
}

TEST(ConnectionPreservingRemesherTest, StructuredTet4RemeshDirectlyProducesValidResult) {
    const std::filesystem::path control_path =
        std::filesystem::path("case") / "cantilever_beam_tet4" / "cantilever_beam_tet4_inp" / "control.json";
    const std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
    if (!std::filesystem::exists(control_path) || !std::filesystem::exists(mesh_path)) {
        GTEST_SKIP() << "cantilever_beam_tet4 Simdroid case is not available";
    }

    DataContext ctx;
    ASSERT_TRUE(SimdroidParser::parse(mesh_path.string(), control_path.string(), ctx));

    SimdroidInspector inspector;
    RemeshOptions options;
    options.target_compression_ratio = 100.0;

    RemeshExecutionResult result =
        ConnectionPreservingRemesher::remesh_structured_tet4(ctx, inspector, options);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.validation.valid);
    ASSERT_EQ(result.after.parts.size(), 1);
    ASSERT_EQ(result.after.parts[0].element_type_counts.size(), 1);
    EXPECT_TRUE(result.after.parts[0].element_type_counts.begin()->first == 304 ||
                result.after.parts[0].element_type_counts.begin()->first == 310);
    EXPECT_GT(result.after.parts[0].original_element_count, 0);
    EXPECT_LT(result.after.original_element_count, result.before.original_element_count);
    EXPECT_TRUE(result.after.parts[0].has_load);
    EXPECT_TRUE(result.after.parts[0].has_constraint);
}

TEST(ConnectionPreservingRemesherTest, DoublePartTet10CasePlansWithTieInterface) {
    const std::filesystem::path control_path =
        std::filesystem::path("case") / "cantilever_beam_tet10" / "doublepart_tet10_inp" / "control.json";
    const std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
    if (!std::filesystem::exists(control_path) || !std::filesystem::exists(mesh_path)) {
        GTEST_SKIP() << "doublepart_tet10 Simdroid case is not available";
    }

    DataContext ctx;
    ASSERT_TRUE(SimdroidParser::parse(mesh_path.string(), control_path.string(), ctx))
        << "failed to parse doublepart_tet10 case";

    SimdroidInspector inspector;
    inspector.build(ctx.registry);

    RemeshOptions options;
    options.target_compression_ratio = 100.0;
    RemeshPlan plan = ConnectionPreservingRemesher::build_plan(ctx.registry, inspector, options);

    EXPECT_EQ(plan.original_element_count, 36036);
    ASSERT_EQ(plan.parts.size(), 2);
    EXPECT_EQ(plan.parts[0].original_element_count, 18018);
    EXPECT_EQ(plan.parts[1].original_element_count, 18018);
    ASSERT_EQ(plan.parts[0].element_type_counts.size(), 1);
    EXPECT_EQ(plan.parts[0].element_type_counts.at(310), 18018);
    ASSERT_EQ(plan.parts[1].element_type_counts.size(), 1);
    EXPECT_EQ(plan.parts[1].element_type_counts.at(310), 18018);

    bool has_contact_interface = false;
    for (const auto& sig : plan.interfaces) {
        if (sig.type == ConnectionType::Contact) has_contact_interface = true;
    }
    EXPECT_TRUE(has_contact_interface);

    auto protected_infos =
        ConnectionPreservingRemesher::extract_protected_entities(ctx.registry, inspector);
    ASSERT_EQ(protected_infos.size(), 2u);
    int total_contact_nodes = 0;
    int total_loaded_nodes = 0;
    int total_constrained_nodes = 0;
    for (const auto& info : protected_infos) {
        total_contact_nodes += static_cast<int>(info.contact_nodes.size());
        total_loaded_nodes += static_cast<int>(info.loaded_nodes.size());
        total_constrained_nodes += static_cast<int>(info.constrained_nodes.size());
    }
    EXPECT_GT(total_contact_nodes, 0);
    EXPECT_GT(total_loaded_nodes, 0);
    EXPECT_GT(total_constrained_nodes, 0);
}

TEST(ConnectionPreservingRemesherTest, StructuredTet4RemeshesDoublePartTet10TieCase) {
    const std::filesystem::path control_path =
        std::filesystem::path("case") / "cantilever_beam_tet10" / "doublepart_tet10_inp" / "control.json";
    const std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
    if (!std::filesystem::exists(control_path) || !std::filesystem::exists(mesh_path)) {
        GTEST_SKIP() << "doublepart_tet10 Simdroid case is not available";
    }

    DataContext ctx;
    ASSERT_TRUE(SimdroidParser::parse(mesh_path.string(), control_path.string(), ctx))
        << "failed to parse doublepart_tet10 case";

    SimdroidInspector inspector;
    RemeshOptions options;
    options.target_compression_ratio = 100.0;

    // Unified dispatch should route to the Tet strategy for Tet10 parts.
    RemeshExecutionResult result =
        ConnectionPreservingRemesher::remesh(ctx, inspector, options);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.validation.valid);

    EXPECT_EQ(result.before.original_element_count, 36036);
    ASSERT_EQ(result.before.parts.size(), 2);
    ASSERT_EQ(result.after.parts.size(), 2);

    // Element count must decrease.
    EXPECT_LT(result.after.original_element_count, result.before.original_element_count);

    // Each part must preserve the Tet10 (310) type signature.
    for (const auto& pp : result.after.parts) {
        ASSERT_EQ(pp.element_type_counts.size(), 1);
        EXPECT_EQ(pp.element_type_counts.at(310), pp.original_element_count);
        EXPECT_GT(pp.original_element_count, 0);
    }

    // The Tie contact interface must be preserved.
    ASSERT_FALSE(result.after.interfaces.empty());
    bool has_contact_after = false;
    for (const auto& sig : result.after.interfaces) {
        if (sig.type == ConnectionType::Contact) has_contact_after = true;
    }
    EXPECT_TRUE(has_contact_after);

    // Each part should still report load/constraint coverage.
    bool any_load = false;
    bool any_constraint = false;
    for (const auto& pp : result.after.parts) {
        if (pp.has_load) any_load = true;
        if (pp.has_constraint) any_constraint = true;
    }
    EXPECT_TRUE(any_load);
    EXPECT_TRUE(any_constraint);
}

TEST(ConnectionPreservingRemesherTest, DetailedValidationPassesWithHealthyRegistry) {
    entt::registry registry;
    nlohmann::json blueprint;

    // Create nodes
    auto n1 = create_node(registry, 1, 0.0, 0.0, 0.0);
    auto n2 = create_node(registry, 2, 1.0, 0.0, 0.0);
    (void)n1; (void)n2;

    // Create a node set with members
    auto ns1 = registry.create();
    registry.emplace<Component::SetName>(ns1, "load_nodes");
    registry.emplace<Component::NodeSetMembers>(ns1, std::vector<entt::entity>{n1, n2});

    // Create elements
    auto prop = registry.create();
    registry.emplace<Component::PropertyID>(prop, 100);
    registry.emplace<Component::SolidProperty>(prop, 308);
    registry.emplace<Component::IntegrationPoints>(prop, 1);

    auto e1 = registry.create();
    registry.emplace<Component::ElementID>(e1, 10);
    registry.emplace<Component::OriginalID>(e1, 10);
    registry.emplace<Component::ElementType>(e1, 308);
    registry.emplace<Component::PropertyRef>(e1, prop);
    registry.emplace<Component::Connectivity>(e1, Component::Connectivity{{n1, n2}});

    // Create an element set with members
    auto es1 = registry.create();
    registry.emplace<Component::SetName>(es1, "element_set_1");
    registry.emplace<Component::ElementSetMembers>(es1, std::vector<entt::entity>{e1});

    // Create a surface set with members
    auto surf = registry.create();
    registry.emplace<Component::SurfaceID>(surf, 100);
    registry.emplace<Component::OriginalID>(surf, 100);
    registry.emplace<Component::SurfaceConnectivity>(surf, Component::SurfaceConnectivity{{n1}});
    registry.emplace<Component::SurfaceParentElement>(surf, e1);

    auto ss1 = registry.create();
    registry.emplace<Component::SetName>(ss1, "surface_set_1");
    registry.emplace<Component::SurfaceSetMembers>(ss1, std::vector<entt::entity>{surf});

    // Apply loads and boundaries
    auto load_def = registry.create();
    registry.emplace<Component::SetName>(load_def, "cload");
    registry.emplace<Component::NodalLoad>(load_def, 1, "z", -1000.0);

    registry.emplace<Component::AppliedLoadRef>(n2, std::vector<entt::entity>{load_def});

    auto bc_def = registry.create();
    registry.emplace<Component::SetName>(bc_def, "spc");
    registry.emplace<Component::BoundarySPC>(bc_def, 1, "all", 0.0);

    registry.emplace<Component::AppliedBoundaryRef>(n1, std::vector<entt::entity>{bc_def});

    // Build a matching blueprint
    blueprint["Load"] = nlohmann::json::object();
    blueprint["Load"]["cload"] = {{"NodeSet", "load_nodes"}};

    blueprint["Constraint"] = nlohmann::json::object();
    blueprint["Constraint"]["Boundary"] = nlohmann::json::object();
    blueprint["Constraint"]["Boundary"]["spc"] = {{"NodeSet", "load_nodes"}};

    blueprint["PartProperty"] = nlohmann::json::object();
    blueprint["PartProperty"]["prop_1"] = {{"EleSet", "element_set_1"}};

    // Build a plan with matching metadata
    RemeshPlan plan;
    plan.original_element_count = 1;
    plan.target_element_count = 1;
    PartRemeshPlan pp;
    pp.part_name = "TestPart";
    pp.original_element_count = 1;
    pp.target_element_count = 1;
    pp.has_load = true;
    pp.has_constraint = true;
    pp.property_type = "SolidProperty";
    pp.material_type = "None";
    pp.element_type_counts[308] = 1;
    plan.parts.push_back(pp);

    auto result = ConnectionPreservingRemesher::validate_preservation_detailed(
        registry, blueprint, plan);
    EXPECT_TRUE(result.valid) << "Errors: " << ::testing::PrintToString(result.errors);
    EXPECT_TRUE(result.errors.empty());
}

TEST(ConnectionPreservingRemesherTest, DetailedValidationDetectsEmptySets) {
    entt::registry registry;
    nlohmann::json blueprint = nlohmann::json::object();

    // Create empty node set
    auto ns1 = registry.create();
    registry.emplace<Component::SetName>(ns1, "empty_node_set");
    registry.emplace<Component::NodeSetMembers>(ns1, std::vector<entt::entity>{});

    // Create empty element set
    auto es1 = registry.create();
    registry.emplace<Component::SetName>(es1, "empty_elem_set");
    registry.emplace<Component::ElementSetMembers>(es1, std::vector<entt::entity>{});

    RemeshPlan plan;
    plan.original_element_count = 0;
    plan.target_element_count = 0;

    auto result = ConnectionPreservingRemesher::validate_preservation_detailed(
        registry, blueprint, plan);
    EXPECT_FALSE(result.valid);
    EXPECT_GE(result.errors.size(), 2u);
}

TEST(ConnectionPreservingRemesherTest, DetailedValidationDetectsMissingLoadSetInBlueprint) {
    entt::registry registry;

    auto n1 = create_node(registry, 1, 0.0, 0.0, 0.0);

    nlohmann::json blueprint;
    blueprint["Load"] = nlohmann::json::object();
    blueprint["Load"]["missing_load"] = {{"NodeSet", "nonexistent_set"}};

    RemeshPlan plan;
    plan.original_element_count = 0;
    plan.target_element_count = 0;

    auto result = ConnectionPreservingRemesher::validate_preservation_detailed(
        registry, blueprint, plan);
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
}

TEST(ConnectionPreservingRemesherTest, DetailedValidationDetectsElementCountMismatch) {
    entt::registry registry;
    nlohmann::json blueprint = nlohmann::json::object();

    auto e1 = registry.create();
    registry.emplace<Component::ElementID>(e1, 1);
    auto e2 = registry.create();
    registry.emplace<Component::ElementID>(e2, 2);

    RemeshPlan plan;
    plan.original_element_count = 1;  // plan says 1, registry has 2
    plan.target_element_count = 1;

    auto result = ConnectionPreservingRemesher::validate_preservation_detailed(
        registry, blueprint, plan);
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
}

TEST(ConnectionPreservingRemesherTest, DetailedValidationDetectsMissingAppliedLoads) {
    entt::registry registry;
    nlohmann::json blueprint = nlohmann::json::object();

    RemeshPlan plan;
    plan.original_element_count = 0;
    plan.target_element_count = 0;
    PartRemeshPlan pp;
    pp.part_name = "LoadedPart";
    pp.has_load = true;
    pp.has_constraint = false;
    plan.parts.push_back(pp);

    // No AppliedLoadRef in registry
    auto result = ConnectionPreservingRemesher::validate_preservation_detailed(
        registry, blueprint, plan);
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
}

TEST(ConnectionPreservingRemesherTest, DetailedValidationDetectsMissingAppliedBoundaries) {
    entt::registry registry;
    nlohmann::json blueprint = nlohmann::json::object();

    RemeshPlan plan;
    plan.original_element_count = 0;
    plan.target_element_count = 0;
    PartRemeshPlan pp;
    pp.part_name = "ConstrainedPart";
    pp.has_load = false;
    pp.has_constraint = true;
    plan.parts.push_back(pp);

    // No AppliedBoundaryRef in registry
    auto result = ConnectionPreservingRemesher::validate_preservation_detailed(
        registry, blueprint, plan);
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
}

TEST(ConnectionPreservingRemesherTest, ExtractsProtectedEntitiesForSharedNodeModel) {
    entt::registry registry;

    auto material = registry.create();
    registry.emplace<Component::MaterialID>(material, 1);
    registry.emplace<Component::MaterialModel>(material, "IsotropicElastic");

    auto property = registry.create();
    registry.emplace<Component::PropertyID>(property, 1);
    registry.emplace<Component::SolidProperty>(property, 304);
    registry.emplace<Component::IntegrationPoints>(property, 1);

    auto n1 = create_node(registry, 1, 0.0, 0.0, 0.0);
    auto n2 = create_node(registry, 2, 1.0, 0.0, 0.0);
    auto n3 = create_node(registry, 3, 0.0, 1.0, 0.0);
    auto n4 = create_node(registry, 4, 0.0, 0.0, 1.0);  // shared node
    auto n5 = create_node(registry, 5, 1.0, 1.0, 0.0);
    auto n6 = create_node(registry, 6, 1.0, 0.0, 1.0);
    auto n7 = create_node(registry, 7, 0.0, 1.0, 1.0);

    auto e1 = create_tet(registry, 10, {n1, n2, n3, n4}, property);
    auto e2 = create_tet(registry, 20, {n4, n5, n6, n7}, property);

    create_part(registry, "Part_A", e1, material, property);
    create_part(registry, "Part_B", e2, material, property);

    // Apply a load to n2 (belongs to Part_A) and a boundary to n6 (belongs to Part_B)
    auto load_def = registry.create();
    registry.emplace<Component::SetName>(load_def, "cload");
    registry.emplace<Component::NodalLoad>(load_def, 1, "z", -1000.0);
    registry.emplace<Component::AppliedLoadRef>(n2, std::vector<entt::entity>{load_def});

    auto bc_def = registry.create();
    registry.emplace<Component::SetName>(bc_def, "spc");
    registry.emplace<Component::BoundarySPC>(bc_def, 1, "all", 0.0);
    registry.emplace<Component::AppliedBoundaryRef>(n6, std::vector<entt::entity>{bc_def});

    SimdroidInspector inspector;
    inspector.build(registry);

    auto protected_infos =
        ConnectionPreservingRemesher::extract_protected_entities(registry, inspector);

    ASSERT_EQ(protected_infos.size(), 2u);

    const ProtectedPartInfo* part_a = nullptr;
    const ProtectedPartInfo* part_b = nullptr;
    for (const auto& info : protected_infos) {
        if (info.part_name == "Part_A") part_a = &info;
        else if (info.part_name == "Part_B") part_b = &info;
    }
    ASSERT_NE(part_a, nullptr);
    ASSERT_NE(part_b, nullptr);

    // n4 is shared between Part_A and Part_B
    EXPECT_EQ(part_a->shared_nodes.size(), 1u);
    EXPECT_EQ(part_a->shared_nodes[0], n4);
    EXPECT_EQ(part_b->shared_nodes.size(), 1u);
    EXPECT_EQ(part_b->shared_nodes[0], n4);

    // n2 carries a load and belongs to Part_A
    EXPECT_EQ(part_a->loaded_nodes.size(), 1u);
    EXPECT_EQ(part_a->loaded_nodes[0], n2);
    EXPECT_EQ(part_b->loaded_nodes.size(), 0u);

    // n6 carries a boundary and belongs to Part_B
    EXPECT_EQ(part_b->constrained_nodes.size(), 1u);
    EXPECT_EQ(part_b->constrained_nodes[0], n6);
    EXPECT_EQ(part_a->constrained_nodes.size(), 0u);

    // No contacts in this model
    EXPECT_EQ(part_a->contact_nodes.size(), 0u);
    EXPECT_EQ(part_b->contact_nodes.size(), 0u);

    // build_plan should also expose the protected counts
    RemeshOptions options;
    options.target_compression_ratio = 100.0;
    RemeshPlan plan = ConnectionPreservingRemesher::build_plan(registry, inspector, options);
    ASSERT_EQ(plan.parts.size(), 2u);
    for (const auto& pp : plan.parts) {
        if (pp.part_name == "Part_A") {
            EXPECT_EQ(pp.protected_shared_node_count, 1);
            EXPECT_EQ(pp.protected_loaded_node_count, 1);
            EXPECT_EQ(pp.protected_constrained_node_count, 0);
        } else if (pp.part_name == "Part_B") {
            EXPECT_EQ(pp.protected_shared_node_count, 1);
            EXPECT_EQ(pp.protected_loaded_node_count, 0);
            EXPECT_EQ(pp.protected_constrained_node_count, 1);
        }
    }
}
