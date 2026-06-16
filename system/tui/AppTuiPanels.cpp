/**
 * TUI Panels - Panel building, list view construction, and view state management.
 */
#include "tui/AppTuiState.h"
#include "tui/ComponentTUI.h"
#include "simdroid/SimdroidInspector.h"
#include "components/mesh_components.h"
#include "components/simdroid_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "AppSession.h"
#include <spdlog/spdlog.h>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/node.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace tui {

// ── View state management ─────────────────────────────────────────────

constexpr std::size_t kMaxViewHistoryDepth = 20;

void save_view_state(TuiAppState& state, std::string label) {
    if (state.view_history.size() >= kMaxViewHistoryDepth) {
        state.view_history.erase(state.view_history.begin());
    }
    ViewState st;
    st.mode = state.left_view_mode;
    st.node_idx = state.node_selected_row;
    st.elem_idx = state.elem_selected_row;
    st.part_idx = state.part_selected_row;
    st.set_idx = state.set_selected_row;
    st.left_focus = state.left_focus;
    st.focus_region = state.focus_region;
    if (state.left_view_mode == LeftViewMode::StaticElement) {
        st.entity_type = state.current_panel_type;
        st.entity_id = state.current_panel_id;
    }
    st.label = std::move(label);
    state.view_history.push_back(std::move(st));
}

void clear_left_view(TuiAppState& state) {
    state.top_panel.reset();
    state.left_view_mode = LeftViewMode::None;
    state.node_rows.clear();
    state.node_selected_row = -1;
    state.elem_rows.clear();
    state.elem_selected_row = -1;
    state.part_rows.clear();
    state.part_selected_row = -1;
    state.set_rows.clear();
    state.set_selected_row = -1;
    state.left_focus = 0.0f;
    state.current_panel_type.clear();
    state.current_panel_id.clear();
}

// ── Focus sync ────────────────────────────────────────────────────────

void sync_nodes_focus(TuiAppState& state) {
    if (state.node_rows.empty() || state.node_selected_row < 0) {
        state.left_focus = 0.0f;
        return;
    }
    state.left_focus = clamp01(
        static_cast<float>(state.node_selected_row + 1) /
        static_cast<float>(state.node_rows.size() + 1));
}

void sync_elems_focus(TuiAppState& state) {
    if (state.elem_rows.empty() || state.elem_selected_row < 0) {
        state.left_focus = 0.0f;
        return;
    }
    state.left_focus = clamp01(
        static_cast<float>(state.elem_selected_row + 1) /
        static_cast<float>(state.elem_rows.size() + 1));
}

void sync_parts_focus(TuiAppState& state) {
    if (state.part_rows.empty() || state.part_selected_row < 0) {
        state.left_focus = 0.0f;
        return;
    }
    state.left_focus = clamp01(
        static_cast<float>(state.part_selected_row + 1) /
        static_cast<float>(state.part_rows.size() + 1));
}

void sync_sets_focus(TuiAppState& state) {
    if (state.set_rows.empty() || state.set_selected_row < 0) {
        state.left_focus = 0.0f;
        return;
    }
    state.left_focus = clamp01(
        static_cast<float>(state.set_selected_row + 1) /
        static_cast<float>(state.set_rows.size() + 1));
}

// ── List view builders ────────────────────────────────────────────────

void build_nodes_list_view(AppSession& session, TuiAppState& state) {
    auto& reg = session.data.registry;
    auto view = reg.view<const ::Component::NodeID, const ::Component::Position>();
    state.node_rows.clear();
    state.node_rows.reserve(view.size_hint());
    for (auto e : view) {
        const auto& id = view.get<const ::Component::NodeID>(e);
        const auto& p = view.get<const ::Component::Position>(e);
        state.node_rows.push_back(NodeListRow{ id.value, p.x, p.y, p.z });
    }
    std::sort(state.node_rows.begin(), state.node_rows.end(),
        [](const NodeListRow& a, const NodeListRow& b) { return a.nid < b.nid; });
    state.left_view_mode = LeftViewMode::NodesList;
    state.top_panel.reset();
    state.current_panel_type.clear();
    state.current_panel_id.clear();
    state.focus_region = FocusRegion::TopLeftView;
}

void build_elements_list_view(AppSession& session, TuiAppState& state) {
    auto& reg = session.data.registry;
    auto view = reg.view<const ::Component::ElementID, const ::Component::ElementType, const ::Component::Connectivity>();
    state.elem_rows.clear();
    state.elem_rows.reserve(view.size_hint());
    for (auto e : view) {
        const int eid = view.get<const ::Component::ElementID>(e).value;
        const int type_id = view.get<const ::Component::ElementType>(e).type_id;
        const auto& conn = view.get<const ::Component::Connectivity>(e);
        std::vector<int> nids;
        nids.reserve(conn.nodes.size());
        for (auto ne : conn.nodes) {
            if (!reg.valid(ne) || !reg.all_of<::Component::NodeID>(ne)) continue;
            nids.push_back(reg.get<::Component::NodeID>(ne).value);
        }
        std::string nodes_str;
        const std::size_t show_n = (std::min<std::size_t>)(nids.size(), 8);
        for (std::size_t i = 0; i < show_n; ++i) {
            if (i > 0) nodes_str += ", ";
            nodes_str += std::to_string(nids[i]);
        }
        if (nids.size() > show_n) nodes_str += " ...";
        state.elem_rows.push_back(ElemListRow{ eid, type_id, std::move(nodes_str) });
    }
    std::sort(state.elem_rows.begin(), state.elem_rows.end(),
        [](const ElemListRow& a, const ElemListRow& b) { return a.eid < b.eid; });
    state.left_view_mode = LeftViewMode::ElementsList;
    state.top_panel.reset();
    state.current_panel_type.clear();
    state.current_panel_id.clear();
    state.focus_region = FocusRegion::TopLeftView;
}

void build_parts_list_view(AppSession& session, TuiAppState& state) {
    auto& reg = session.data.registry;
    auto view = reg.view<const ::Component::SimdroidPart>();
    state.part_rows.clear();
    state.part_rows.reserve(static_cast<std::size_t>(view.size()));
    for (auto e : view) {
        const auto& part = view.get<const ::Component::SimdroidPart>(e);
        std::size_t count = 0;
        if (reg.valid(part.element_set) && reg.all_of<::Component::ElementSetMembers>(part.element_set)) {
            count = reg.get<::Component::ElementSetMembers>(part.element_set).members.size();
        }
        std::string element_set_name = "-";
        if (reg.valid(part.element_set) && reg.all_of<::Component::SetName>(part.element_set)) {
            element_set_name = reg.get<::Component::SetName>(part.element_set).value;
        }
        std::string material_id = "-";
        if (reg.valid(part.material) && reg.all_of<::Component::MaterialID>(part.material)) {
            material_id = std::to_string(reg.get<::Component::MaterialID>(part.material).value);
        }
        std::string section_id = "-";
        if (reg.valid(part.section) && reg.all_of<::Component::PropertyID>(part.section)) {
            section_id = std::to_string(reg.get<::Component::PropertyID>(part.section).value);
        }
        state.part_rows.push_back(PartListRow{
            part.name,
            std::move(element_set_name),
            std::move(material_id),
            std::move(section_id),
            count
        });
    }
    std::sort(state.part_rows.begin(), state.part_rows.end(),
        [](const PartListRow& a, const PartListRow& b) { return a.name < b.name; });
    state.left_view_mode = LeftViewMode::PartsList;
    state.top_panel.reset();
    state.current_panel_type.clear();
    state.current_panel_id.clear();
    state.focus_region = FocusRegion::TopLeftView;
}

void build_sets_list_view(AppSession& session, TuiAppState& state) {
    auto& reg = session.data.registry;
    state.set_rows.clear();

    // Collect NodeSets
    {
        auto view = reg.view<const ::Component::SetName, const ::Component::NodeSetMembers>();
        for (auto e : view) {
            const auto& name = view.get<const ::Component::SetName>(e).value;
            const auto& members = view.get<const ::Component::NodeSetMembers>(e).members;
            state.set_rows.push_back(SetListRow{ name, "node", members.size() });
        }
    }
    // Collect ElementSets
    {
        auto view = reg.view<const ::Component::SetName, const ::Component::ElementSetMembers>();
        for (auto e : view) {
            const auto& name = view.get<const ::Component::SetName>(e).value;
            const auto& members = view.get<const ::Component::ElementSetMembers>(e).members;
            state.set_rows.push_back(SetListRow{ name, "elem", members.size() });
        }
    }
    std::sort(state.set_rows.begin(), state.set_rows.end(),
        [](const SetListRow& a, const SetListRow& b) {
            if (a.type != b.type) return a.type < b.type;
            return a.name < b.name;
        });
    state.left_view_mode = LeftViewMode::SetList;
    state.top_panel.reset();
    state.current_panel_type.clear();
    state.current_panel_id.clear();
    state.focus_region = FocusRegion::TopLeftView;
}

// ── Restore view state ────────────────────────────────────────────────

bool restore_view_state(AppSession& session, TuiAppState& state) {
    if (state.view_history.empty())
        return false;
    ViewState st = std::move(state.view_history.back());
    state.view_history.pop_back();

    if (st.mode == LeftViewMode::NodesList) {
        build_nodes_list_view(session, state);
        if (!state.node_rows.empty()) {
            const int max_idx = static_cast<int>(state.node_rows.size()) - 1;
            state.node_selected_row = (std::max)(0, (std::min)(max_idx, st.node_idx));
        } else {
            state.node_selected_row = -1;
        }
        sync_nodes_focus(state);
    } else if (st.mode == LeftViewMode::ElementsList) {
        build_elements_list_view(session, state);
        if (!state.elem_rows.empty()) {
            const int max_idx = static_cast<int>(state.elem_rows.size()) - 1;
            state.elem_selected_row = (std::max)(0, (std::min)(max_idx, st.elem_idx));
        } else {
            state.elem_selected_row = -1;
        }
        sync_elems_focus(state);
    } else if (st.mode == LeftViewMode::PartsList) {
        build_parts_list_view(session, state);
        if (!state.part_rows.empty()) {
            const int max_idx = static_cast<int>(state.part_rows.size()) - 1;
            state.part_selected_row = (std::max)(0, (std::min)(max_idx, st.part_idx));
        } else {
            state.part_selected_row = -1;
        }
        sync_parts_focus(state);
    } else if (st.mode == LeftViewMode::SetList) {
        build_sets_list_view(session, state);
        if (!state.set_rows.empty()) {
            const int max_idx = static_cast<int>(state.set_rows.size()) - 1;
            state.set_selected_row = (std::max)(0, (std::min)(max_idx, st.set_idx));
        } else {
            state.set_selected_row = -1;
        }
        sync_sets_focus(state);
    } else if (st.mode == LeftViewMode::StaticElement && !st.entity_type.empty() && !st.entity_id.empty()) {
        (void)open_panel_in_top_view(session, state, st.entity_type, st.entity_id, false);
    } else {
        clear_left_view(state);
    }

    state.left_focus = st.left_focus;
    state.focus_region = st.focus_region;
    return true;
}

// ── Panel building ────────────────────────────────────────────────────

bool open_panel_in_top_view(AppSession& session, TuiAppState& state,
                            const std::string& type, const std::string& id_or_name,
                            bool push_history)
{
    // 拷贝本地副本，防止 state.top_panel 被替换时调用方传入的引用（如 shared_rows 中的 r.type）变成悬挂引用
    const std::string type_local = type;
    const std::string id_local   = id_or_name;

    if (push_history) {
        save_view_state(state, "panel " + type_local + " " + id_local);
    }
    PanelEntityKind kind = PanelEntityKind::Unknown;
    std::string display_id;
    entt::entity e = resolve_panel_entity(
        session.data.registry, &session.inspector, type_local, id_local, &kind, &display_id);
    if (e == entt::null) {
        spdlog::error("Panel: entity not found. Ensure mesh is loaded and index built.");
        return false;
    }

    const char* kind_str_c = "Entity";
    switch (kind) {
        case PanelEntityKind::Node:     kind_str_c = "Node";     break;
        case PanelEntityKind::Element:  kind_str_c = "Element";  break;
        case PanelEntityKind::Part:     kind_str_c = "Part";     break;
        case PanelEntityKind::Set:      kind_str_c = "Set";      break;
        case PanelEntityKind::Material: kind_str_c = "Material"; break;
        case PanelEntityKind::Section:  kind_str_c = "Section";  break;
        default: break;
    }
    const std::string kind_str = kind_str_c;

    // ── Set: build a scrollable list panel ────────────────────────────
    if (kind == PanelEntityKind::Set) {
        struct SetMemberRow {
            int id;
            std::string type;  // "node" or "elem"
            std::string extra;
        };
        std::vector<SetMemberRow> rows;
        std::string member_type;
        auto& reg = session.data.registry;

        // NodeSetMembers
        if (reg.all_of<::Component::NodeSetMembers>(e)) {
            const auto& members = reg.get<::Component::NodeSetMembers>(e).members;
            member_type = "node";
            for (auto me : members) {
                if (!reg.valid(me) || !reg.all_of<::Component::NodeID>(me)) continue;
                const int nid = reg.get<::Component::NodeID>(me).value;
                std::string extra;
                if (reg.all_of<::Component::Position>(me)) {
                    const auto& p = reg.get<::Component::Position>(me);
                    std::ostringstream sx, sy, sz;
                    sx.setf(std::ios::fixed); sy.setf(std::ios::fixed); sz.setf(std::ios::fixed);
                    sx << std::setprecision(6) << p.x;
                    sy << std::setprecision(6) << p.y;
                    sz << std::setprecision(6) << p.z;
                    extra = sx.str() + ", " + sy.str() + ", " + sz.str();
                }
                rows.push_back(SetMemberRow{ nid, "node", std::move(extra) });
            }
        }
        // ElementSetMembers
        if (reg.all_of<::Component::ElementSetMembers>(e)) {
            const auto& members = reg.get<::Component::ElementSetMembers>(e).members;
            member_type = "elem";
            for (auto me : members) {
                if (!reg.valid(me) || !reg.all_of<::Component::ElementID>(me)) continue;
                const int eid = reg.get<::Component::ElementID>(me).value;
                std::string extra;
                if (reg.all_of<::Component::ElementType>(me)) {
                    extra = "type=" + std::to_string(reg.get<::Component::ElementType>(me).type_id);
                }
                rows.push_back(SetMemberRow{ eid, "elem", std::move(extra) });
            }
        }
        std::sort(rows.begin(), rows.end(),
            [](const SetMemberRow& a, const SetMemberRow& b) { return a.id < b.id; });

        const bool is_node_set = (member_type == "node");
        const std::string set_title = is_node_set ? "NodeSet: " + id_local : "ElementSet: " + id_local;

        auto shared_rows = std::make_shared<std::vector<SetMemberRow>>(std::move(rows));
        auto shared_selected = std::make_shared<int>(shared_rows->empty() ? -1 : 0);

        ftxui::Component dummy = ftxui::Container::Vertical({});
        state.top_panel = ftxui::Renderer(dummy, [&state, shared_rows, shared_selected, is_node_set, set_title]() {
            using namespace ftxui;
            const int selected = *shared_selected;
            const int total_count = static_cast<int>(shared_rows->size());
            const int margin = 30;
            const int anchor_idx = selected >= 0 ? selected : 0;
            const int start_idx = (std::max)(0, anchor_idx - margin);
            const int end_idx = (std::min)(total_count, anchor_idx + margin + 1);

            Elements lines;
            if (is_node_set) {
                lines.push_back(hbox({
                    text(" NodeID ") | bold, text(" | "),
                    text(" Position (X, Y, Z) ") | bold,
                }) | color(Color::Cyan));
            } else {
                lines.push_back(hbox({
                    text(" ElementID ") | bold, text(" | "),
                    text(" Type ") | bold,
                }) | color(Color::YellowLight));
            }
            lines.push_back(separatorLight());

            for (int i = start_idx; i < end_idx; ++i) {
                const auto& r = (*shared_rows)[static_cast<std::size_t>(i)];
                Element row;
                if (is_node_set) {
                    row = hbox({
                        text(" " + std::to_string(r.id) + " ") | color(Color::Cyan),
                        text(" | "),
                        text(" " + r.extra + " "),
                    });
                } else {
                    row = hbox({
                        text(" " + std::to_string(r.id) + " ") | color(Color::Cyan),
                        text(" | "),
                        text(" " + r.extra + " ") | color(Color::YellowLight),
                    });
                }
                if (i == selected)
                    row = row | inverted | focus;
                lines.push_back(std::move(row));
            }

            return vbox({
                hbox({
                    text(" NovaFEA ") | bgcolor(Color::Blue) | color(Color::White) | bold,
                    text(" Set ") | color(Color::Cyan),
                    filler(),
                    text(set_title) | dim,
                }) | border,
                hbox({
                    text(" " + set_title + " "),
                    filler(),
                    text(
                        "Viewing: " + std::to_string(total_count == 0 ? 0 : start_idx + 1) +
                        "-" + std::to_string(end_idx) +
                        " / " + std::to_string(total_count)
                    ) | dim
                }),
                separator(),
                vbox(std::move(lines)) | flex,
                separator(),
                text("Scroll: ArrowUp ArrowDown / PgUp PgDn   Enter: jump to item   Esc: back") | dim,
            });
        });

        const std::string set_id_capture = id_local;
        state.top_panel = ftxui::CatchEvent(*state.top_panel, [&session, &state, shared_rows, shared_selected, set_id_capture](ftxui::Event ev) -> bool {
            const int total = static_cast<int>(shared_rows->size());
            if (total == 0) return false;
            const int max_idx = total - 1;
            int& sel = *shared_selected;
            if (ev == ftxui::Event::ArrowUp) {
                sel = (std::max)(0, sel - 1);
                return true;
            }
            if (ev == ftxui::Event::ArrowDown) {
                sel = (std::min)(max_idx, sel + 1);
                return true;
            }
            if (ev == ftxui::Event::PageUp) {
                sel = (std::max)(0, sel - 10);
                return true;
            }
            if (ev == ftxui::Event::PageDown) {
                sel = (std::min)(max_idx, sel + 10);
                return true;
            }
            if (ev == ftxui::Event::Return && sel >= 0 && sel < total) {
                const auto& r = (*shared_rows)[static_cast<std::size_t>(sel)];
                save_view_state(state, "panel set " + set_id_capture);
                (void)open_panel_in_top_view(session, state, r.type, std::to_string(r.id), false);
                return true;
            }
            return false;
        });

        state.left_view_mode = LeftViewMode::StaticElement;
        state.left_focus = 0.0f;
        state.focus_region = FocusRegion::TopLeftView;
        state.current_panel_type = type_local;
        state.current_panel_id = id_local;
        return true;
    }

    // ── General entity panel (Node / Element / Part / Material / Section) ─
    std::vector<ftxui::Component> link_buttons;

    if (kind == PanelEntityKind::Node && session.inspector.is_built && session.data.registry.all_of<::Component::NodeID>(e)) {
        const int nid = session.data.registry.get<::Component::NodeID>(e).value;
        auto it = session.inspector.nid_to_elems.find(nid);
        if (it != session.inspector.nid_to_elems.end()) {
            const auto& elem_ids = it->second;
            const std::size_t show = (std::min<std::size_t>)(elem_ids.size(), 40);
            for (std::size_t i = 0; i < show; ++i) {
                const int eid = elem_ids[i];
                link_buttons.push_back(ftxui::Button(std::to_string(eid), [&session, &state, eid]() {
                    (void)open_panel_in_top_view(session, state, "elem", std::to_string(eid), true);
                }));
            }
        }
    }

    if (kind == PanelEntityKind::Element && session.inspector.is_built && session.data.registry.all_of<::Component::Connectivity>(e)) {
        const auto& c = session.data.registry.get<::Component::Connectivity>(e);
        std::vector<int> nids;
        nids.reserve(c.nodes.size());
        for (auto ne : c.nodes) {
            if (!session.data.registry.valid(ne) || !session.data.registry.all_of<::Component::NodeID>(ne)) continue;
            nids.push_back(session.data.registry.get<::Component::NodeID>(ne).value);
        }
        const std::size_t show = (std::min<std::size_t>)(nids.size(), 40);
        for (std::size_t i = 0; i < show; ++i) {
            const int nid = nids[i];
            link_buttons.push_back(ftxui::Button(std::to_string(nid), [&session, &state, nid]() {
                (void)open_panel_in_top_view(session, state, "node", std::to_string(nid), true);
            }));
        }
    }

    if (kind == PanelEntityKind::Part && session.data.registry.all_of<::Component::SimdroidPart>(e)) {
        const auto& part = session.data.registry.get<::Component::SimdroidPart>(e);

        // Add Material jump button
        if (session.data.registry.valid(part.material)) {
            std::string mat_id;
            if (session.data.registry.all_of<::Component::MaterialID>(part.material)) {
                mat_id = std::to_string(session.data.registry.get<::Component::MaterialID>(part.material).value);
            }
            if (!mat_id.empty()) {
                link_buttons.push_back(ftxui::Button("Material ID: " + mat_id, [&session, &state, part]() {
                    if (session.data.registry.valid(part.material)) {
                        std::string mid;
                        if (session.data.registry.all_of<::Component::MaterialID>(part.material)) {
                            mid = std::to_string(session.data.registry.get<::Component::MaterialID>(part.material).value);
                        }
                        if (!mid.empty())
                            (void)open_panel_in_top_view(session, state, "material", mid, true);
                    }
                }));
            }
        }

        // Add Section jump button
        if (session.data.registry.valid(part.section)) {
            std::string sec_id;
            if (session.data.registry.all_of<::Component::PropertyID>(part.section)) {
                sec_id = std::to_string(session.data.registry.get<::Component::PropertyID>(part.section).value);
            }
            if (!sec_id.empty()) {
                link_buttons.push_back(ftxui::Button("Section ID: " + sec_id, [&session, &state, part]() {
                    if (session.data.registry.valid(part.section)) {
                        std::string pid;
                        if (session.data.registry.all_of<::Component::PropertyID>(part.section)) {
                            pid = std::to_string(session.data.registry.get<::Component::PropertyID>(part.section).value);
                        }
                        if (!pid.empty())
                            (void)open_panel_in_top_view(session, state, "section", pid, true);
                    }
                }));
            }
        }

        // Add ElementSet jump button
        if (session.data.registry.valid(part.element_set)) {
            std::string set_name;
            if (session.data.registry.all_of<::Component::SetName>(part.element_set)) {
                set_name = session.data.registry.get<::Component::SetName>(part.element_set).value;
            }
            if (!set_name.empty()) {
                link_buttons.push_back(ftxui::Button("Element Set: " + set_name, [&session, &state, part]() {
                    if (session.data.registry.valid(part.element_set)) {
                        std::string sn;
                        if (session.data.registry.all_of<::Component::SetName>(part.element_set)) {
                            sn = session.data.registry.get<::Component::SetName>(part.element_set).value;
                        }
                        if (!sn.empty())
                            (void)open_panel_in_top_view(session, state, "set", sn, true);
                    }
                }));
            }
        }
    }

    const bool has_links = !link_buttons.empty();
    ftxui::Component links =
        has_links ? ftxui::Container::Vertical(std::move(link_buttons)) : ftxui::Container::Vertical({});

    ftxui::Component panel_root = links;
    state.top_panel = ftxui::Renderer(panel_root, [&session, e, kind, kind_str, display_id, links, has_links] {
        using namespace ftxui;
        Elements component_views;
        for (const auto& entry : ComponentTUIRegistry::instance().entries()) {
            if (entry.has_component(session.data.registry, e)) {
                component_views.push_back(
                    window(text(entry.display_name) | bold, entry.render(session.data.registry, e, &session.inspector)));
            }
        }
        if (kind == PanelEntityKind::Node) {
            component_views.push_back(window(text("Force path") | bold,
                force_path_element(session.data.registry, e, &session.inspector)));
        }

        Elements body;
        body.push_back(
            hbox({
                text(" NovaFEA ") | bgcolor(Color::Blue) | color(Color::White) | bold,
                text(" Universal Inspector ") | color(Color::Cyan),
                filler(),
                text(std::string(kind_str) + " " + display_id) | dim,
            }) | border);
        body.push_back(vbox(component_views));
        if (has_links) {
            const std::string title =
                (kind == PanelEntityKind::Node) ? "Elements (jump)" :
                (kind == PanelEntityKind::Element) ? "Contains Nodes (jump)" : "Related (jump)";
            body.push_back(window(text(title) | bold, links->Render() | flex));
        }
        body.push_back(text("Tip: type another command below. Use 'help' for quick help.") | dim);
        return vbox(std::move(body));
    });
    state.left_view_mode = LeftViewMode::StaticElement;
    state.left_focus = 0.0f;
    state.focus_region = FocusRegion::TopLeftView;
    state.current_panel_type = type_local;
    state.current_panel_id = id_local;
    return true;
}

} // namespace tui
