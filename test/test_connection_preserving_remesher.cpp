#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include "remesh/ConnectionPreservingRemesher.h"
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
    registry.emplace<Component::SolidProperty>(property, 304, 1, "none");

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
