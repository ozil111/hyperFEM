#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include <fstream>
#include "exporter_json/JsonExporter.h"
#include "parser_json/JsonParser.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "components/simdroid_components.h"
#include "components/load_components.h"
#include "components/analysis_component.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class JsonExporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a simple model in memory
        
        // 1. Material
        auto mat = registry.create();
        registry.emplace<Component::MaterialID>(mat, 1);
        registry.emplace<Component::LinearElasticParams>(mat, 7850.0, 210000.0, 0.3);
        
        // 2. Property
        auto prop = registry.create();
        registry.emplace<Component::PropertyID>(prop, 1);
        registry.emplace<Component::SolidProperty>(prop, 1);
        registry.emplace<Component::IntegrationPoints>(prop, 2);
        registry.emplace<Component::HourglassControl>(prop, "eas");
        
        // 3. SimdroidPart (links mat and prop)
        auto part_e = registry.create();
        auto& part = registry.emplace<Component::SimdroidPart>(part_e);
        part.material = mat;
        part.section = prop;
        part.name = "Part_1";

        // 4. Nodes
        for (int i = 0; i < 8; ++i) {
            auto node = registry.create();
            registry.emplace<Component::NodeID>(node, i + 1);
            registry.emplace<Component::Position>(node, (double)(i % 2), (double)((i / 2) % 2), (double)(i / 4));
            nodes.push_back(node);
        }
        
        // 5. Element
        auto elem = registry.create();
        registry.emplace<Component::ElementID>(elem, 1);
        registry.emplace<Component::ElementType>(elem, 308);
        registry.emplace<Component::PropertyRef>(elem, prop);
        Component::Connectivity conn;
        conn.nodes = nodes;
        registry.emplace<Component::Connectivity>(elem, std::move(conn));

        // 6. NodeSet
        auto ns_e = registry.create();
        registry.emplace<Component::NodeSetID>(ns_e, 1);
        registry.emplace<Component::NodeSetMembers>(ns_e, std::vector<entt::entity>{nodes[0], nodes[1]});

        // 7. Curve
        auto curve_e = registry.create();
        registry.emplace<Component::CurveID>(curve_e, 1);
        registry.emplace<Component::Curve>(curve_e, "linear", std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0});

        // 8. Load
        auto load_e = registry.create();
        registry.emplace<Component::LoadID>(load_e, 1);
        registry.emplace<Component::NodalLoad>(load_e, 1, "z", -100.0, curve_e);
        
        // Apply load to nodes in nodeset
        auto& applied = registry.emplace<Component::AppliedLoadRef>(nodes[0]);
        applied.load_entities.push_back(load_e);
        auto& applied2 = registry.emplace<Component::AppliedLoadRef>(nodes[1]);
        applied2.load_entities.push_back(load_e);

        // 9. Analysis
        auto analysis_e = registry.create();
        registry.emplace<Component::AnalysisID>(analysis_e, 1);
        registry.emplace<Component::AnalysisType>(analysis_e, "static");
        registry.emplace<Component::EndTime>(analysis_e, 1.0);
        registry.emplace<Component::FixedTimeStep>(analysis_e, 0.1);
        data_context.analysis_entity = analysis_e;

        // 10. Output
        auto output_e = registry.create();
        registry.emplace<Component::NodeOutput>(output_e, std::vector<std::string>{"displacement"});
        registry.emplace<Component::OutputIntervalTime>(output_e, 0.1);
        data_context.output_entity = output_e;
    }

    entt::registry& registry = data_context.registry;
    DataContext data_context;
    std::vector<entt::entity> nodes;
};

TEST_F(JsonExporterTest, ExportAndParseBack) {
    std::string test_file = "test_export.json";
    
    // Save to JSON
    ASSERT_TRUE(JsonExporter::save(test_file, data_context));
    
    // Verify file exists and is valid JSON
    std::ifstream f(test_file);
    ASSERT_TRUE(f.is_open());
    json j = json::parse(f);
    f.close();
    
    EXPECT_TRUE(j.contains("material"));
    EXPECT_TRUE(j.contains("property"));
    EXPECT_TRUE(j.contains("mesh"));
    EXPECT_TRUE(j["mesh"].contains("nodes"));
    EXPECT_TRUE(j["mesh"].contains("elements"));
    EXPECT_TRUE(j.contains("nodeset"));
    EXPECT_TRUE(j.contains("load"));
    EXPECT_TRUE(j.contains("analysis"));
    EXPECT_TRUE(j.contains("output"));

    // Parse back using JsonParser
    DataContext data_context2;
    ASSERT_TRUE(JsonParser::parse(test_file, data_context2));
    
    // Verify data_context2 has the same data
    auto& reg2 = data_context2.registry;
    EXPECT_EQ(reg2.view<Component::MaterialID>().size(), 1);
    EXPECT_EQ(reg2.view<Component::PropertyID>().size(), 1);
    EXPECT_EQ(reg2.view<Component::NodeID>().size(), 8);
    EXPECT_EQ(reg2.view<Component::ElementID>().size(), 1);
    EXPECT_EQ(reg2.view<Component::NodeSetID>().size(), 1);
    EXPECT_EQ(reg2.view<Component::LoadID>().size(), 1);
    
    // Clean up
    std::remove(test_file.c_str());
}
