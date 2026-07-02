// test_abaqus_parser.cpp
// Unit tests for the Abaqus .inp parser

#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include <string>
#include <cmath>

#include "DataContext.h"
#include "parser_abaqus/AbaqusParser.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "components/load_components.h"
#include "components/analysis_component.h"
#include "components/simdroid_components.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

static std::string test_inp_path() {
    return std::string(TEST_DATA_DIR) + "/abaqus_T01.inp";
}

// Helper: check entity validity without triggering gtest's entity printer
// (which causes entt template instantiation issues with 64-bit IDs).
static bool is_valid(entt::entity e) {
    return e != entt::null;
}

// ---------------------------------------------------------------------------
// Fixture: parse T01.inp once for all tests
// ---------------------------------------------------------------------------
class AbaqusParserTest : public ::testing::Test {
protected:
    DataContext ctx;

    void SetUp() override {
        std::string path = test_inp_path();
        bool ok = AbaqusParser::parse(path, ctx);
        ASSERT_TRUE(ok) << "Failed to parse: " << path;
    }

    void TearDown() override {
        ctx.clear();
    }

    /// Find a node entity by its NodeID value, or entt::null if not found.
    entt::entity find_node(int id) {
        auto view = ctx.registry.view<const Component::NodeID>();
        for (auto e : view) {
            if (view.get<const Component::NodeID>(e).value == id)
                return e;
        }
        return entt::null;
    }

    /// Find an element entity by its ElementID value, or entt::null if not found.
    entt::entity find_element(int id) {
        auto view = ctx.registry.view<const Component::ElementID>();
        for (auto e : view) {
            if (view.get<const Component::ElementID>(e).value == id)
                return e;
        }
        return entt::null;
    }

    /// Find a set entity by name, or entt::null if not found.
    entt::entity find_set(const std::string& name) {
        auto view = ctx.registry.view<const Component::SetName>();
        for (auto e : view) {
            if (view.get<const Component::SetName>(e).value == name)
                return e;
        }
        return entt::null;
    }
};

// ---------------------------------------------------------------------------
// Node tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, NodeCount) {
    auto count = ctx.registry.view<const Component::Position>().size();
    EXPECT_EQ(count, 8u);
}

TEST_F(AbaqusParserTest, NodeCoordinates) {
    // Node 1: (1, 1, 1), Node 8: (0, 0, 0)
    auto n1 = find_node(1);
    ASSERT_TRUE(is_valid(n1));
    auto& p1 = ctx.registry.get<Component::Position>(n1);
    EXPECT_DOUBLE_EQ(p1.x, 1.0);
    EXPECT_DOUBLE_EQ(p1.y, 1.0);
    EXPECT_DOUBLE_EQ(p1.z, 1.0);

    auto n8 = find_node(8);
    ASSERT_TRUE(is_valid(n8));
    auto& p8 = ctx.registry.get<Component::Position>(n8);
    EXPECT_DOUBLE_EQ(p8.x, 0.0);
    EXPECT_DOUBLE_EQ(p8.y, 0.0);
    EXPECT_DOUBLE_EQ(p8.z, 0.0);
}

TEST_F(AbaqusParserTest, NodeIDsAttached) {
    for (int i = 1; i <= 8; ++i) {
        auto e = find_node(i);
        ASSERT_TRUE(is_valid(e)) << "Node " << i << " not found";
        EXPECT_EQ(ctx.registry.get<Component::NodeID>(e).value, i);
    }
}

// ---------------------------------------------------------------------------
// Element tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, ElementCount) {
    auto count = ctx.registry.view<const Component::Connectivity>().size();
    EXPECT_EQ(count, 1u);
}

TEST_F(AbaqusParserTest, ElementType) {
    auto e = find_element(1);
    ASSERT_TRUE(is_valid(e));
    // C3D8R maps to 308 (Hexa8)
    EXPECT_EQ(ctx.registry.get<Component::ElementType>(e).type_id, 308);
}

TEST_F(AbaqusParserTest, ElementConnectivity) {
    auto e = find_element(1);
    ASSERT_TRUE(is_valid(e));
    auto& conn = ctx.registry.get<Component::Connectivity>(e);
    EXPECT_EQ(conn.nodes.size(), 8u);

    // T01.inp: element 1 connects nodes 5, 6, 8, 7, 1, 2, 4, 3
    int expected_ids[] = {5, 6, 8, 7, 1, 2, 4, 3};
    for (size_t i = 0; i < 8; ++i) {
        auto& nid = ctx.registry.get<Component::NodeID>(conn.nodes[i]);
        EXPECT_EQ(nid.value, expected_ids[i]) << "Node index " << i;
    }
}

TEST_F(AbaqusParserTest, ElementHasPropertyRef) {
    auto e = find_element(1);
    ASSERT_TRUE(is_valid(e));
    EXPECT_TRUE(ctx.registry.all_of<Component::PropertyRef>(e));
}

// ---------------------------------------------------------------------------
// Node set tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, NodeSetGenerate) {
    // *NSET, NSET=Part-1-1_Set-1, GENERATE  -> 1..8
    auto s = find_set("Part-1-1_Set-1");
    ASSERT_TRUE(is_valid(s));
    auto& members = ctx.registry.get<Component::NodeSetMembers>(s);
    EXPECT_EQ(members.members.size(), 8u);
}

TEST_F(AbaqusParserTest, NodeSetDirect) {
    // *NSET, NSET=Part-1_Part-1-1-1  -> nodes 2, 4, 6, 8
    auto s = find_set("Part-1_Part-1-1-1");
    ASSERT_TRUE(is_valid(s));
    auto& members = ctx.registry.get<Component::NodeSetMembers>(s);
    EXPECT_EQ(members.members.size(), 4u);
}

TEST_F(AbaqusParserTest, NodeSetReference) {
    // *NSET, NSET=fix  -> references Part-1_Part-1-1-1 (4 nodes)
    auto s = find_set("fix");
    ASSERT_TRUE(is_valid(s));
    auto& members = ctx.registry.get<Component::NodeSetMembers>(s);
    EXPECT_EQ(members.members.size(), 4u);
}

TEST_F(AbaqusParserTest, NodeSetLoadReference) {
    // *NSET, NSET=load  -> references Part-1_Part-1-1-2 (nodes 1,3,5,7 = 4 nodes)
    auto s = find_set("load");
    ASSERT_TRUE(is_valid(s));
    auto& members = ctx.registry.get<Component::NodeSetMembers>(s);
    EXPECT_EQ(members.members.size(), 4u);
}

// ---------------------------------------------------------------------------
// Element set tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, ElementSetCount) {
    // Elsets: HMprop_Part-1-1_Set-1, Part-1-1_Set-1, fix
    auto view = ctx.registry.view<const Component::ElementSetMembers>();
    EXPECT_GE(view.size(), 2u);
}

TEST_F(AbaqusParserTest, ElementSetMembers) {
    auto s = find_set("fix");
    ASSERT_TRUE(is_valid(s));
    ASSERT_TRUE(ctx.registry.all_of<Component::ElementSetMembers>(s));
    auto& members = ctx.registry.get<Component::ElementSetMembers>(s);
    EXPECT_EQ(members.members.size(), 1u);
}

// ---------------------------------------------------------------------------
// Material tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, MaterialExists) {
    auto view = ctx.registry.view<const Component::MaterialID>();
    EXPECT_EQ(view.size(), 1u);
}

TEST_F(AbaqusParserTest, MaterialProperties) {
    // Material-1: density=1000, E=21000000, nu=0.3
    auto view = ctx.registry.view<const Component::LinearElasticParams>();
    ASSERT_EQ(view.size(), 1u);
    auto e = view.front();
    auto& p = ctx.registry.get<Component::LinearElasticParams>(e);
    EXPECT_DOUBLE_EQ(p.rho, 1000.0);
    EXPECT_DOUBLE_EQ(p.E, 21000000.0);
    EXPECT_DOUBLE_EQ(p.nu, 0.3);
}

// ---------------------------------------------------------------------------
// Solid section / property / part tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, PropertyExists) {
    auto view = ctx.registry.view<const Component::PropertyID>();
    EXPECT_EQ(view.size(), 1u);
}

TEST_F(AbaqusParserTest, PartBinding) {
    // SimdroidPart should bind element_set + material + section
    auto view = ctx.registry.view<const Component::SimdroidPart>();
    ASSERT_EQ(view.size(), 1u);
    auto e = view.front();
    auto& part = ctx.registry.get<Component::SimdroidPart>(e);
    EXPECT_TRUE(is_valid(part.material));
    EXPECT_TRUE(is_valid(part.section));
    EXPECT_TRUE(is_valid(part.element_set));
}

// ---------------------------------------------------------------------------
// Amplitude / curve tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, AmplitudeCurve) {
    // Amp-1: (0,0) -> (1,1)
    auto view = ctx.registry.view<const Component::Curve>();
    ASSERT_EQ(view.size(), 1u);
    auto e = view.front();
    auto& c = ctx.registry.get<Component::Curve>(e);
    ASSERT_EQ(c.x.size(), 2u);
    ASSERT_EQ(c.y.size(), 2u);
    EXPECT_DOUBLE_EQ(c.x[0], 0.0);
    EXPECT_DOUBLE_EQ(c.y[0], 0.0);
    EXPECT_DOUBLE_EQ(c.x[1], 1.0);
    EXPECT_DOUBLE_EQ(c.y[1], 1.0);
}

// ---------------------------------------------------------------------------
// Analysis / step tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, AnalysisEntity) {
    EXPECT_TRUE(is_valid(ctx.analysis_entity));
}

TEST_F(AbaqusParserTest, AnalysisType) {
    ASSERT_TRUE(is_valid(ctx.analysis_entity));
    ASSERT_TRUE(ctx.registry.all_of<Component::AnalysisType>(ctx.analysis_entity));
    auto& at = ctx.registry.get<Component::AnalysisType>(ctx.analysis_entity);
    EXPECT_EQ(at.value, "DynamicExplicit");
}

TEST_F(AbaqusParserTest, AnalysisTimeSettings) {
    ASSERT_TRUE(is_valid(ctx.analysis_entity));
    ASSERT_TRUE(ctx.registry.all_of<Component::FixedTimeStep>(ctx.analysis_entity));
    auto& ts = ctx.registry.get<Component::FixedTimeStep>(ctx.analysis_entity);
    EXPECT_DOUBLE_EQ(ts.value, 0.001);

    ASSERT_TRUE(ctx.registry.all_of<Component::EndTime>(ctx.analysis_entity));
    auto& et = ctx.registry.get<Component::EndTime>(ctx.analysis_entity);
    EXPECT_DOUBLE_EQ(et.value, 1.0);
}

// ---------------------------------------------------------------------------
// Boundary condition tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, BoundaryExists) {
    // *BOUNDARY: fix, dof 2, value 0.0
    auto view = ctx.registry.view<const Component::BoundarySPC>();
    ASSERT_EQ(view.size(), 1u);
    auto e = view.front();
    auto& spc = ctx.registry.get<Component::BoundarySPC>(e);
    EXPECT_EQ(spc.dof, "y");   // dof 2 -> y
    EXPECT_DOUBLE_EQ(spc.value, 0.0);
}

TEST_F(AbaqusParserTest, BoundaryAppliedToNodes) {
    // fix set has 4 nodes; each should have AppliedBoundaryRef
    auto s = find_set("fix");
    ASSERT_TRUE(is_valid(s));
    auto& members = ctx.registry.get<Component::NodeSetMembers>(s);
    for (auto node : members.members) {
        EXPECT_TRUE(ctx.registry.all_of<Component::AppliedBoundaryRef>(node))
            << "Node missing AppliedBoundaryRef";
    }
}

// ---------------------------------------------------------------------------
// Concentrated load tests
// ---------------------------------------------------------------------------
TEST_F(AbaqusParserTest, CloadExists) {
    // *CLOAD, AMPLITUDE=Amp-1: load, dof 2, value 100.0
    auto view = ctx.registry.view<const Component::NodalLoad>();
    ASSERT_EQ(view.size(), 1u);
    auto e = view.front();
    auto& load = ctx.registry.get<Component::NodalLoad>(e);
    EXPECT_EQ(load.dof, "y");  // dof 2 -> y
    EXPECT_DOUBLE_EQ(load.value, 100.0);
    // Should reference the amplitude curve
    EXPECT_TRUE(is_valid(load.curve_entity));
}

TEST_F(AbaqusParserTest, CloadAppliedToNodes) {
    // load set has 4 nodes; each should have AppliedLoadRef
    auto s = find_set("load");
    ASSERT_TRUE(is_valid(s));
    auto& members = ctx.registry.get<Component::NodeSetMembers>(s);
    for (auto node : members.members) {
        EXPECT_TRUE(ctx.registry.all_of<Component::AppliedLoadRef>(node))
            << "Node missing AppliedLoadRef";
    }
}

// ---------------------------------------------------------------------------
// Edge case: non-existent file
// ---------------------------------------------------------------------------
TEST(AbaqusParserErrorTest, FileNotFound) {
    DataContext ctx;
    bool ok = AbaqusParser::parse("nonexistent_file.inp", ctx);
    EXPECT_FALSE(ok);
}
