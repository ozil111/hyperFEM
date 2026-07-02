/**
 * TUI Universal Inspection Panel - Registry implementation.
 */
#include "tui/ComponentTUI.h"
#include "components/mesh_components.h"
#include "components/load_components.h"
#include "components/material_components.h"
#include "components/simdroid_components.h"
#include "components/property_components.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace tui {

namespace {

Element matrix_6x6_element(const double* D) {
    Elements rows;
    for (int i = 0; i < 6; ++i) {
        std::ostringstream line;
        for (int j = 0; j < 6; ++j)
            line << std::setw(12) << std::fixed << std::setprecision(4) << D[i * 6 + j];
        rows.push_back(text("    " + line.str()));
    }
    return vbox(std::move(rows));
}

std::string entity_set_name(entt::registry& reg, entt::entity e) {
    if (reg.valid(e) && reg.all_of<::Component::SetName>(e))
        return reg.get<::Component::SetName>(e).value;
    return "-";
}

std::string entity_material_id(entt::registry& reg, entt::entity e) {
    if (reg.valid(e) && reg.all_of<::Component::MaterialID>(e))
        return std::to_string(reg.get<::Component::MaterialID>(e).value);
    return "-";
}

std::string entity_property_id(entt::registry& reg, entt::entity e) {
    if (reg.valid(e) && reg.all_of<::Component::PropertyID>(e))
        return std::to_string(reg.get<::Component::PropertyID>(e).value);
    return "-";
}

std::string shell_type_name(int type_id) {
    switch (type_id) {
        case 1:  return "Shell";
        case 11: return "SandwichShell";
        default: return "Shell(type_id=" + std::to_string(type_id) + ")";
    }
}

void init_registry() {
    auto& r = ComponentTUIRegistry::instance();

    r.register_component<::Component::Position>("Position",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& p = reg.get<::Component::Position>(e);
            std::ostringstream sx, sy, sz;
            sx << std::fixed << std::setprecision(6) << p.x;
            sy << std::fixed << std::setprecision(6) << p.y;
            sz << std::fixed << std::setprecision(6) << p.z;
            return hbox({
                text(" X: ") | color(Color::Red),   text(sx.str()),
                text(" Y: ") | color(Color::Green), text(sy.str()),
                text(" Z: ") | color(Color::Blue),  text(sz.str())
            });
        });

    r.register_component<::Component::NodeID>("NodeID",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            return text("  " + std::to_string(reg.get<::Component::NodeID>(e).value));
        });

    r.register_component<::Component::ElementID>("ElementID",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            return text("  " + std::to_string(reg.get<::Component::ElementID>(e).value));
        });

    r.register_component<::Component::Connectivity>("Connectivity",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& c = reg.get<::Component::Connectivity>(e);
            Elements node_list;
            for (entt::entity ne : c.nodes) {
                if (reg.valid(ne) && reg.all_of<::Component::NodeID>(ne))
                    node_list.push_back(text(std::to_string(reg.get<::Component::NodeID>(ne).value)) | border);
                else
                    node_list.push_back(text("?") | border);
            }
            return hbox(std::move(node_list)) | flex;
        });

    r.register_component<::Component::ElementType>("ElementType",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            return text("  type_id = " + std::to_string(reg.get<::Component::ElementType>(e).type_id));
        });

    r.register_component<::Component::SimdroidPart>("SimdroidPart",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& p = reg.get<::Component::SimdroidPart>(e);
            Elements lines = {
                text("  name = " + p.name),
                text("  element_set = " + entity_set_name(reg, p.element_set)),
                text("  material_id = " + entity_material_id(reg, p.material)),
                text("  section_id = " + entity_property_id(reg, p.section)),
            };
            return vbox(std::move(lines));
        });

    r.register_component<::Component::MaterialModel>("MaterialModel",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            return text("  " + reg.get<::Component::MaterialModel>(e).value);
        });

    r.register_component<::Component::PropertyID>("PropertyID",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            return text("  " + std::to_string(reg.get<::Component::PropertyID>(e).value));
        });

    r.register_component<::Component::SolidProperty>("SolidProperty",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& p = reg.get<::Component::SolidProperty>(e);
            Elements lines = {
                text("  Type = Solid"),
                text("  type_id = " + std::to_string(p.type_id)),
            };
            if (reg.all_of<::Component::Formulation>(e))
                lines.push_back(text("  Formulation = " + reg.get<::Component::Formulation>(e).value));
            if (reg.all_of<::Component::SmallStrain>(e))
                lines.push_back(text("  SmallStrain = " + reg.get<::Component::SmallStrain>(e).value));
            if (reg.all_of<::Component::IntegrationPoints>(e))
                lines.push_back(text("  IntegrationPoints = " + std::to_string(reg.get<::Component::IntegrationPoints>(e).value)));
            if (reg.all_of<::Component::HourglassControl>(e))
                lines.push_back(text("  HourglassControl = " + reg.get<::Component::HourglassControl>(e).value));
            return vbox(std::move(lines));
        });

    r.register_component<::Component::ShellProperty>("ShellProperty",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& p = reg.get<::Component::ShellProperty>(e);
            Elements lines = {
                text("  Type = " + shell_type_name(p.type_id)),
                text("  SmallStrain = -"),
                text("  Formulation = -"),
            };
            return vbox(std::move(lines));
        });

    r.register_component<::Component::SolidShellProperty>("SolidShellProperty",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& p = reg.get<::Component::SolidShellProperty>(e);
            Elements lines = {
                text("  Type = SolidShell"),
                text("  SmallStrain = " + (p.small_strain.value.empty() ? "-" : p.small_strain.value)),
                text("  Formulation = " + (p.formulation.value.empty() ? "-" : p.formulation.value)),
            };
            return vbox(std::move(lines));
        });

    r.register_component<::Component::SolidShCompProperty>("SolidShCompProperty",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& p = reg.get<::Component::SolidShCompProperty>(e);
            Elements lines = {
                text("  Type = SolidShComp"),
                text("  SmallStrain = " + (p.small_strain.value.empty() ? "-" : p.small_strain.value)),
                text("  Formulation = " + (p.formulation.value.empty() ? "-" : p.formulation.value)),
            };
            return vbox(std::move(lines));
        });

    r.register_component<::Component::LinearElasticParams>("LinearElasticParams",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& le = reg.get<::Component::LinearElasticParams>(e);
            std::ostringstream ss;
            ss << "  rho = " << le.rho << ", E = " << le.E << ", nu = " << le.nu;
            return text(ss.str());
        });

    r.register_component<::Component::LinearElasticMatrix>("LinearElasticMatrix",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& lem = reg.get<::Component::LinearElasticMatrix>(e);
            Elements parts = { text("  is_initialized = " + std::string(lem.is_initialized ? "true" : "false")) };
            if (lem.is_initialized)
                parts.push_back(vbox({ text("  D (6x6):"), matrix_6x6_element(lem.D) }));
            return vbox(std::move(parts));
        });

    r.register_component<::Component::AppliedLoadRef>("AppliedLoadRef",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& ref = reg.get<::Component::AppliedLoadRef>(e);
            Elements lines = { text("  loads: " + std::to_string(ref.load_entities.size()) + " ref(s)") };
            for (size_t i = 0; i < ref.load_entities.size(); ++i) {
                entt::entity le = ref.load_entities[i];
                if (!reg.valid(le)) { lines.push_back(text("    [" + std::to_string(i) + "] (invalid)")); continue; }
                if (reg.all_of<::Component::NodalLoad>(le)) {
                    const auto& nl = reg.get<::Component::NodalLoad>(le);
                    lines.push_back(text("    [" + std::to_string(i) + "] NodalLoad dof=" + nl.dof + " value=" + std::to_string(nl.value)));
                } else if (reg.all_of<::Component::BaseAccelerationLoad>(le)) {
                    const auto& ba = reg.get<::Component::BaseAccelerationLoad>(le);
                    std::ostringstream ss;
                    ss << "    [" << i << "] BaseAcceleration ax=" << ba.ax << " ay=" << ba.ay << " az=" << ba.az;
                    lines.push_back(text(ss.str()));
                } else
                    lines.push_back(text("    [" + std::to_string(i) + "] (other)"));
            }
            return vbox(std::move(lines));
        });

    r.register_component<::Component::AppliedBoundaryRef>("AppliedBoundaryRef",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& ref = reg.get<::Component::AppliedBoundaryRef>(e);
            Elements lines = { text("  boundaries: " + std::to_string(ref.boundary_entities.size()) + " ref(s)") };
            for (size_t i = 0; i < ref.boundary_entities.size(); ++i) {
                entt::entity be = ref.boundary_entities[i];
                if (!reg.valid(be)) { lines.push_back(text("    [" + std::to_string(i) + "] (invalid)")); continue; }
                if (reg.all_of<::Component::BoundarySPC>(be)) {
                    const auto& spc = reg.get<::Component::BoundarySPC>(be);
                    lines.push_back(text("    [" + std::to_string(i) + "] SPC dof=" + spc.dof + " value=" + std::to_string(spc.value)));
                } else
                    lines.push_back(text("    [" + std::to_string(i) + "] (other)"));
            }
            return vbox(std::move(lines));
        });

    r.register_component<::Component::ForcePathNode>("ForcePathNode",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& fp = reg.get<::Component::ForcePathNode>(e);
            std::ostringstream ss;
            ss << "  weight = " << fp.weight
               << ", is_load_point = " << (fp.is_load_point ? "true" : "false")
               << ", is_constraint_point = " << (fp.is_constraint_point ? "true" : "false");
            return text(ss.str());
        });

    r.register_component<::Component::SetName>("SetName",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            return text("  " + reg.get<::Component::SetName>(e).value);
        });

    r.register_component<::Component::ElementSetMembers>("ElementSetMembers",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& m = reg.get<::Component::ElementSetMembers>(e);
            std::string line = "  count = " + std::to_string(m.members.size()) + "\n  ";
            const size_t show = (std::min)(m.members.size(), size_t(20));
            for (size_t i = 0; i < show; ++i) {
                if (i > 0) line += ", ";
                entt::entity me = m.members[i];
                if (reg.valid(me) && reg.all_of<::Component::ElementID>(me))
                    line += std::to_string(reg.get<::Component::ElementID>(me).value);
                else
                    line += "?";
            }
            if (m.members.size() > show) line += " ...";
            return text(line);
        });

    r.register_component<::Component::NodeSetMembers>("NodeSetMembers",
        [](entt::registry& reg, entt::entity e, SimdroidInspector*) -> Element {
            const auto& m = reg.get<::Component::NodeSetMembers>(e);
            std::string line = "  count = " + std::to_string(m.members.size()) + "\n  ";
            const size_t show = (std::min)(m.members.size(), size_t(20));
            for (size_t i = 0; i < show; ++i) {
                if (i > 0) line += ", ";
                entt::entity me = m.members[i];
                if (reg.valid(me) && reg.all_of<::Component::NodeID>(me))
                    line += std::to_string(reg.get<::Component::NodeID>(me).value);
                else
                    line += "?";
            }
            if (m.members.size() > show) line += " ...";
            return text(line);
        });
}

} // anonymous namespace

ComponentTUIRegistry& ComponentTUIRegistry::instance() {
    static ComponentTUIRegistry inst;
    static bool once = false;
    if (!once) {
        once = true;
        init_registry();
    }
    return inst;
}

} // namespace tui
