/**
 * TUI Universal Inspection Panel - Interactive application TUI entry point.
 * Layout: top-left = view area, top-right = output log, bottom = command line.
 */
#include "tui/AppTuiState.h"
#include "tui/ComponentTUI.h"
#include "AppSession.h"
#include "CommandProcessor.h"
#include <spdlog/spdlog.h>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <algorithm>
#include <sstream>

namespace tui {

void run_app_tui(AppSession& session) {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();

    std::string input;
    TuiAppState state;

    auto input_component = Input(&input, "command...");

    auto ui = Renderer(input_component, [&] {
        Element top_left;
        if (state.left_view_mode == LeftViewMode::NodesList) {
            top_left = render_nodes_list_element(state);
        } else if (state.left_view_mode == LeftViewMode::ElementsList) {
            top_left = render_elements_list_element(state);
        } else if (state.left_view_mode == LeftViewMode::PartsList) {
            top_left = render_parts_list_element(state);
        } else if (state.left_view_mode == LeftViewMode::SetList) {
            top_left = render_sets_list_element(state);
        } else if (state.top_panel.has_value()) {
            top_left = state.top_panel.value()->Render();
        } else {
            top_left = text("No active TUI view. Try: list_nodes, list_elements, list_parts, list_sets, panel ...") | dim;
        }

        const bool left_focused = state.focus_region == FocusRegion::TopLeftView;
        const bool right_focused = state.focus_region == FocusRegion::TopRightOutput;
        const bool command_focused = state.focus_region == FocusRegion::BottomCommand;
        const std::vector<TuiLogLine> log_lines = tui_log_lines_snapshot();

        Element left_box =
            window(
                text(left_focused ? " TUI View [TAB] " : " TUI View ") | bold,
                top_left
                    | yframe
                    | vscroll_indicator
                    | flex);

        Element right_box =
            window(
                text(right_focused ? " Output [TAB] " : " Output ") | bold,
                status_lines_element(log_lines)
                    | focusPositionRelative(0.0f, state.right_focus)
                    | yframe
                    | vscroll_indicator
                    | flex);

        Element top_row = hbox({
            left_box | flex,
            right_box | size(WIDTH, EQUAL, 56),
        }) | flex;

        Element bottom_box =
            window(
                text(command_focused ? " Command [TAB] " : " Command ") | bold,
                hbox({
                    text("NovaFEA> ") | color(Color::Cyan),
                    input_component->Render() | flex,
                }));

        return vbox({
            top_row | flex,
            bottom_box,
        }) | border;
    });

    ui = CatchEvent(ui, [&](Event event) {
        if (event == Event::Tab) {
            const int idx = (static_cast<int>(state.focus_region) + 1) % 3;
            state.focus_region = static_cast<FocusRegion>(idx);
            return true;
        }

        if (state.focus_region == FocusRegion::TopLeftView &&
            state.left_view_mode == LeftViewMode::StaticElement &&
            state.top_panel.has_value()) {
            if (state.top_panel.value()->OnEvent(event))
                return true;
        }

        // ── Command-line history navigation ────────────────────────────
        if (state.focus_region == FocusRegion::BottomCommand) {
            const int history_size = static_cast<int>(state.input_history.size());
            if (event == Event::ArrowUp) {
                if (history_size <= 0) return true;
                if (state.history_index == -1) {
                    state.input_backup = input;
                    state.history_index = history_size - 1;
                } else {
                    state.history_index = (std::max)(0, state.history_index - 1);
                }
                input = state.input_history[static_cast<std::size_t>(state.history_index)];
                return true;
            }
            if (event == Event::ArrowDown) {
                if (history_size <= 0) return true;
                if (state.history_index == -1) return true;
                if (state.history_index < history_size - 1) {
                    state.history_index = state.history_index + 1;
                    input = state.input_history[static_cast<std::size_t>(state.history_index)];
                } else {
                    input = state.input_backup;
                    state.history_index = -1;
                }
                return true;
            }
        }

        if (state.focus_region != FocusRegion::BottomCommand && event.is_character()) {
            return true;
        }

        // ── Enter key handling ─────────────────────────────────────────
        if (event == Event::Return) {
            if (state.focus_region == FocusRegion::TopLeftView) {
                if (state.left_view_mode == LeftViewMode::NodesList && !state.node_rows.empty() &&
                    state.node_selected_row >= 0 && state.node_selected_row < static_cast<int>(state.node_rows.size())) {
                    save_view_state(state, "list_nodes");
                    (void)open_panel_in_top_view(session, state, "node",
                        std::to_string(state.node_rows[static_cast<std::size_t>(state.node_selected_row)].nid), false);
                } else if (state.left_view_mode == LeftViewMode::ElementsList && !state.elem_rows.empty() &&
                    state.elem_selected_row >= 0 && state.elem_selected_row < static_cast<int>(state.elem_rows.size())) {
                    save_view_state(state, "list_elements");
                    (void)open_panel_in_top_view(session, state, "elem",
                        std::to_string(state.elem_rows[static_cast<std::size_t>(state.elem_selected_row)].eid), false);
                } else if (state.left_view_mode == LeftViewMode::PartsList && !state.part_rows.empty() &&
                    state.part_selected_row >= 0 && state.part_selected_row < static_cast<int>(state.part_rows.size())) {
                    save_view_state(state, "list_parts");
                    (void)open_panel_in_top_view(session, state, "part",
                        state.part_rows[static_cast<std::size_t>(state.part_selected_row)].name, false);
                } else if (state.left_view_mode == LeftViewMode::SetList && !state.set_rows.empty() &&
                    state.set_selected_row >= 0 && state.set_selected_row < static_cast<int>(state.set_rows.size())) {
                    save_view_state(state, "list_sets");
                    (void)open_panel_in_top_view(session, state, "set",
                        state.set_rows[static_cast<std::size_t>(state.set_selected_row)].name, false);
                }
                return true;
            }
            if (state.focus_region != FocusRegion::BottomCommand) {
                return true;
            }
            const std::string cmd = input;
            input.clear();
            if (!cmd.empty()) {
                if (state.input_history.empty() || cmd != state.input_history.back()) {
                    state.input_history.push_back(cmd);
                }
            }
            state.history_index = -1;

            if (cmd.empty()) return true;

            if (cmd == "quit" || cmd == "exit") {
                session.is_running = false;
                screen.Exit();
                return true;
            }

            // Help
            if (cmd == "help") {
                std::vector<std::string> help = {
                    "Commands:",
                    "  import <file>",
                    "  info",
                    "  list_nodes",
                    "  list_elements",
                    "  list_parts",
                    "  list_sets",
                    "  panel node <nid>",
                    "  panel elem <eid>",
                    "  panel part <name>",
                    "  panel set <name>",
                    "  panel material <id_or_name>",
                    "  panel section <id_or_name>",
                    "  set_material <mid> <component> <param> <value>",
                    "  quit / exit",
                    "",
                    "Note: other commands are supported; they will execute and log to the normal logger.",
                };
                ftxui::Component dummy = Container::Vertical({});
                state.top_panel = Renderer(dummy, [help = std::move(help)]() {
                    return window(text(" Help ") | bold, status_lines_element(help));
                });
                state.left_view_mode = LeftViewMode::StaticElement;
                state.left_focus = 0.0f;
                state.focus_region = FocusRegion::TopLeftView;
                return true;
            }

            // list_* commands
            if (cmd == "list_nodes") {
                save_view_state(state, "list_nodes");
                build_nodes_list_view(session, state);
                state.node_selected_row = state.node_rows.empty() ? -1 : 0;
                sync_nodes_focus(state);
                state.focus_region = FocusRegion::TopLeftView;
                spdlog::info("Nodes (total {}):", state.node_rows.size());
                for (const auto& r : state.node_rows) {
                    spdlog::info("  NID={}  X={:.6f}  Y={:.6f}  Z={:.6f}", r.nid, r.x, r.y, r.z);
                }
                return true;
            }

            if (cmd == "list_elements") {
                save_view_state(state, "list_elements");
                build_elements_list_view(session, state);
                state.elem_selected_row = state.elem_rows.empty() ? -1 : 0;
                sync_elems_focus(state);
                state.focus_region = FocusRegion::TopLeftView;
                spdlog::info("Elements (total {}):", state.elem_rows.size());
                for (const auto& r : state.elem_rows) {
                    spdlog::info("  EID={}  Type={}  Nodes=[{}]", r.eid, r.type_id, r.nodes);
                }
                return true;
            }

            if (cmd == "list_parts") {
                save_view_state(state, "list_parts");
                build_parts_list_view(session, state);
                state.part_selected_row = state.part_rows.empty() ? -1 : 0;
                sync_parts_focus(state);
                state.focus_region = FocusRegion::TopLeftView;
                spdlog::info("Parts (total {}):", state.part_rows.size());
                for (const auto& r : state.part_rows) {
                    spdlog::info("  {}  ElementSet={}  MatID={}  SecID={}  Elems={}",
                                 r.name, r.element_set, r.material_id, r.section_id, r.elem_count);
                }
                return true;
            }

            if (cmd == "list_sets") {
                save_view_state(state, "list_sets");
                build_sets_list_view(session, state);
                state.set_selected_row = state.set_rows.empty() ? -1 : 0;
                sync_sets_focus(state);
                state.focus_region = FocusRegion::TopLeftView;
                spdlog::info("Sets (total {}):", state.set_rows.size());
                for (const auto& r : state.set_rows) {
                    spdlog::info("  {}  type={}  members={}", r.name, r.type, r.member_count);
                }
                return true;
            }

            // panel command
            if (starts_with(cmd, "panel ")) {
                std::stringstream ss(cmd);
                std::string keyword, type, id_or_name;
                ss >> keyword >> type;
                std::getline(ss >> std::ws, id_or_name);
                if (type.empty() || id_or_name.empty()) {
                    spdlog::error("Usage: panel <type> <id_or_name>  (type: node|elem|element|part|set|material|section)");
                    return true;
                }
                spdlog::info("Panel opened: {} {}", type, id_or_name);
                (void)open_panel_in_top_view(session, state, type, id_or_name, true);
                return true;
            }

            // Fallback: execute existing command processor
            state.view_history.clear();
            clear_left_view(state);
            process_command(cmd, session);
            return true;
        }

        // ── Escape: back navigation ────────────────────────────────────
        if (event == Event::Escape) {
            if (!restore_view_state(session, state)) {
                clear_left_view(state);
            }
            return true;
        }

        // ── Keyboard navigation for list views ─────────────────────────
        if (state.focus_region == FocusRegion::TopLeftView &&
            state.left_view_mode == LeftViewMode::NodesList &&
            !state.node_rows.empty()) {
            const int max_idx = static_cast<int>(state.node_rows.size()) - 1;
            if (event == Event::ArrowUp) {
                state.node_selected_row = (std::max)(0, state.node_selected_row - 1);
                sync_nodes_focus(state);
                return true;
            }
            if (event == Event::ArrowDown) {
                state.node_selected_row = (std::min)(max_idx, state.node_selected_row + 1);
                sync_nodes_focus(state);
                return true;
            }
            if (event == Event::PageUp) {
                state.node_selected_row = (std::max)(0, state.node_selected_row - 10);
                sync_nodes_focus(state);
                return true;
            }
            if (event == Event::PageDown) {
                state.node_selected_row = (std::min)(max_idx, state.node_selected_row + 10);
                sync_nodes_focus(state);
                return true;
            }
        }

        if (state.focus_region == FocusRegion::TopLeftView &&
            state.left_view_mode == LeftViewMode::ElementsList &&
            !state.elem_rows.empty()) {
            const int max_idx = static_cast<int>(state.elem_rows.size()) - 1;
            if (event == Event::ArrowUp) {
                state.elem_selected_row = (std::max)(0, state.elem_selected_row - 1);
                sync_elems_focus(state);
                return true;
            }
            if (event == Event::ArrowDown) {
                state.elem_selected_row = (std::min)(max_idx, state.elem_selected_row + 1);
                sync_elems_focus(state);
                return true;
            }
            if (event == Event::PageUp) {
                state.elem_selected_row = (std::max)(0, state.elem_selected_row - 10);
                sync_elems_focus(state);
                return true;
            }
            if (event == Event::PageDown) {
                state.elem_selected_row = (std::min)(max_idx, state.elem_selected_row + 10);
                sync_elems_focus(state);
                return true;
            }
        }

        if (state.focus_region == FocusRegion::TopLeftView &&
            state.left_view_mode == LeftViewMode::PartsList &&
            !state.part_rows.empty()) {
            const int max_idx = static_cast<int>(state.part_rows.size()) - 1;
            if (event == Event::ArrowUp) {
                state.part_selected_row = (std::max)(0, state.part_selected_row - 1);
                sync_parts_focus(state);
                return true;
            }
            if (event == Event::ArrowDown) {
                state.part_selected_row = (std::min)(max_idx, state.part_selected_row + 1);
                sync_parts_focus(state);
                return true;
            }
            if (event == Event::PageUp) {
                state.part_selected_row = (std::max)(0, state.part_selected_row - 10);
                sync_parts_focus(state);
                return true;
            }
            if (event == Event::PageDown) {
                state.part_selected_row = (std::min)(max_idx, state.part_selected_row + 10);
                sync_parts_focus(state);
                return true;
            }
        }

        if (state.focus_region == FocusRegion::TopLeftView &&
            state.left_view_mode == LeftViewMode::SetList &&
            !state.set_rows.empty()) {
            const int max_idx = static_cast<int>(state.set_rows.size()) - 1;
            if (event == Event::ArrowUp) {
                state.set_selected_row = (std::max)(0, state.set_selected_row - 1);
                sync_sets_focus(state);
                return true;
            }
            if (event == Event::ArrowDown) {
                state.set_selected_row = (std::min)(max_idx, state.set_selected_row + 1);
                sync_sets_focus(state);
                return true;
            }
            if (event == Event::PageUp) {
                state.set_selected_row = (std::max)(0, state.set_selected_row - 10);
                sync_sets_focus(state);
                return true;
            }
            if (event == Event::PageDown) {
                state.set_selected_row = (std::min)(max_idx, state.set_selected_row + 10);
                sync_sets_focus(state);
                return true;
            }
        }

        // ── Generic arrow/page scrolling for view panes ────────────────
        if (state.focus_region == FocusRegion::TopLeftView && event == Event::ArrowUp) {
            state.left_focus = clamp01(state.left_focus - 0.03f);
            return true;
        }
        if (state.focus_region == FocusRegion::TopLeftView && event == Event::ArrowDown) {
            state.left_focus = clamp01(state.left_focus + 0.03f);
            return true;
        }
        if (state.focus_region == FocusRegion::TopLeftView && event == Event::PageUp) {
            state.left_focus = clamp01(state.left_focus - 0.12f);
            return true;
        }
        if (state.focus_region == FocusRegion::TopLeftView && event == Event::PageDown) {
            state.left_focus = clamp01(state.left_focus + 0.12f);
            return true;
        }
        if (state.focus_region == FocusRegion::TopRightOutput && event == Event::ArrowUp) {
            state.right_focus = clamp01(state.right_focus - 0.03f);
            return true;
        }
        if (state.focus_region == FocusRegion::TopRightOutput && event == Event::ArrowDown) {
            state.right_focus = clamp01(state.right_focus + 0.03f);
            return true;
        }
        if (state.focus_region == FocusRegion::TopRightOutput && event == Event::PageUp) {
            state.right_focus = clamp01(state.right_focus - 0.12f);
            return true;
        }
        if (state.focus_region == FocusRegion::TopRightOutput && event == Event::PageDown) {
            state.right_focus = clamp01(state.right_focus + 0.12f);
            return true;
        }

        // ── Mouse wheel handling ───────────────────────────────────────
        if (event.is_mouse()) {
            const auto& m = event.mouse();
            if (state.focus_region == FocusRegion::TopLeftView &&
                state.left_view_mode == LeftViewMode::NodesList &&
                !state.node_rows.empty()) {
                const int max_idx = static_cast<int>(state.node_rows.size()) - 1;
                if (m.button == Mouse::WheelUp) {
                    state.node_selected_row = (std::max)(0, state.node_selected_row - 3);
                    sync_nodes_focus(state);
                    return true;
                }
                if (m.button == Mouse::WheelDown) {
                    state.node_selected_row = (std::min)(max_idx, state.node_selected_row + 3);
                    sync_nodes_focus(state);
                    return true;
                }
            }
            if (state.focus_region == FocusRegion::TopLeftView &&
                state.left_view_mode == LeftViewMode::ElementsList &&
                !state.elem_rows.empty()) {
                const int max_idx = static_cast<int>(state.elem_rows.size()) - 1;
                if (m.button == Mouse::WheelUp) {
                    state.elem_selected_row = (std::max)(0, state.elem_selected_row - 3);
                    sync_elems_focus(state);
                    return true;
                }
                if (m.button == Mouse::WheelDown) {
                    state.elem_selected_row = (std::min)(max_idx, state.elem_selected_row + 3);
                    sync_elems_focus(state);
                    return true;
                }
            }
            if (state.focus_region == FocusRegion::TopLeftView &&
                state.left_view_mode == LeftViewMode::PartsList &&
                !state.part_rows.empty()) {
                const int max_idx = static_cast<int>(state.part_rows.size()) - 1;
                if (m.button == Mouse::WheelUp) {
                    state.part_selected_row = (std::max)(0, state.part_selected_row - 3);
                    sync_parts_focus(state);
                    return true;
                }
                if (m.button == Mouse::WheelDown) {
                    state.part_selected_row = (std::min)(max_idx, state.part_selected_row + 3);
                    sync_parts_focus(state);
                    return true;
                }
            }
            if (state.focus_region == FocusRegion::TopLeftView &&
                state.left_view_mode == LeftViewMode::SetList &&
                !state.set_rows.empty()) {
                const int max_idx = static_cast<int>(state.set_rows.size()) - 1;
                if (m.button == Mouse::WheelUp) {
                    state.set_selected_row = (std::max)(0, state.set_selected_row - 3);
                    sync_sets_focus(state);
                    return true;
                }
                if (m.button == Mouse::WheelDown) {
                    state.set_selected_row = (std::min)(max_idx, state.set_selected_row + 3);
                    sync_sets_focus(state);
                    return true;
                }
            }
            if (state.focus_region == FocusRegion::TopLeftView && m.button == Mouse::WheelUp) {
                state.left_focus = clamp01(state.left_focus - 0.06f);
                return true;
            }
            if (state.focus_region == FocusRegion::TopLeftView && m.button == Mouse::WheelDown) {
                state.left_focus = clamp01(state.left_focus + 0.06f);
                return true;
            }
            if (state.focus_region == FocusRegion::TopRightOutput && m.button == Mouse::WheelUp) {
                state.right_focus = clamp01(state.right_focus - 0.06f);
                return true;
            }
            if (state.focus_region == FocusRegion::TopRightOutput && m.button == Mouse::WheelDown) {
                state.right_focus = clamp01(state.right_focus + 0.06f);
                return true;
            }

            // Mouse-wheel scroll for Output pane when hovering it (no TAB required).
            if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
                const int dimx = screen.dimx();
                const int dimy = screen.dimy();
                const int output_width = 56;
                const int output_left = (std::max)(0, dimx - output_width - 1);
                const int top_area_bottom = (std::max)(0, dimy - 3); // command box is ~3 lines tall
                const bool over_output = (m.x >= output_left) && (m.y >= 0) && (m.y < top_area_bottom);
                if (over_output) {
                    state.right_focus = clamp01(state.right_focus + (m.button == Mouse::WheelUp ? -0.06f : 0.06f));
                    return true;
                }
            }
        }
        return false;
    });

    session.is_running = true;
    screen.Loop(ui);
}

} // namespace tui
