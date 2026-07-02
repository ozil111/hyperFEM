/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "CommandInternal.h"
#include <spdlog/spdlog.h>
#include "components/material_components.h"
#include "components/property_components.h"
#include <string>

namespace cmd::property {

using namespace cmd::detail;

// =======================================================
// Material parameter editing
// =======================================================
void handle_set_material(std::stringstream& ss, AppSession& session) {
    int mid;
    std::string component_name, param_name;
    double value;
    if (!(ss >> mid >> component_name >> param_name >> value)) {
        spdlog::error("Usage: set_material <material_id> <component> <param> <value>");
        spdlog::info("  Component: LinearElastic | IsotropicPlastic | RateDependentPlastic | HyperelasticMode");
        spdlog::info("  Params (LinearElastic): rho, E, nu");
        spdlog::info("  Params (IsotropicPlastic): yield_stress_A, hardening_coef_B, hardening_exp_n, rate_coef_C, hardening_mode, temperature_exp_m, melt_temperature, env_temperature, ref_strain_rate, specific_heat");
        spdlog::info("  Params (RateDependentPlastic): hardening_mode, failure_plastic_strain, fail_begin_tensile_strain, fail_end_tensile_strain, elem_del_tensile_strain");
        spdlog::info("  Params (HyperelasticMode): order, nu");
        return;
    }

    auto& registry = session.data.registry;
    entt::entity mat_e = find_material_by_id(registry, mid);
    if (mat_e == entt::null) {
        spdlog::error("Material {} not found.", mid);
        return;
    }

    auto print_current = [&]() {
        const auto& model = registry.get<::Component::MaterialModel>(mat_e).value;
        spdlog::info("Material {} (Model: {}): parameter updated.", mid, model);
    };

    if (component_name == "LinearElastic") {
        if (!registry.all_of<::Component::LinearElasticParams>(mat_e)) {
            spdlog::error("Material {} does not have LinearElasticParams.", mid);
            return;
        }
        auto& p = registry.get<::Component::LinearElasticParams>(mat_e);
        if (param_name == "rho")      { p.rho = value; }
        else if (param_name == "E")   { p.E = value; }
        else if (param_name == "nu")  { p.nu = value; }
        else { spdlog::error("Unknown LinearElastic param: '{}'. Valid: rho, E, nu", param_name); return; }
        spdlog::info("Material {} LinearElastic.{} = {}", mid, param_name, value);
        print_current();
    }
    else if (component_name == "IsotropicPlastic") {
        if (!registry.all_of<::Component::IsotropicPlasticParams>(mat_e)) {
            spdlog::error("Material {} does not have IsotropicPlasticParams.", mid);
            return;
        }
        auto& p = registry.get<::Component::IsotropicPlasticParams>(mat_e);
        if (param_name == "yield_stress_A")            p.yield_stress_A = value;
        else if (param_name == "hardening_coef_B")     p.hardening_coef_B = value;
        else if (param_name == "hardening_exp_n")      p.hardening_exp_n = value;
        else if (param_name == "rate_coef_C")          p.rate_coef_C = value;
        else if (param_name == "hardening_mode")       p.hardening_mode = value;
        else if (param_name == "temperature_exp_m")    p.temperature_exp_m = value;
        else if (param_name == "melt_temperature")     p.melt_temperature = value;
        else if (param_name == "env_temperature")      p.env_temperature = value;
        else if (param_name == "ref_strain_rate")      p.ref_strain_rate = value;
        else if (param_name == "specific_heat")        p.specific_heat = value;
        else { spdlog::error("Unknown IsotropicPlastic param: '{}'.", param_name); return; }
        spdlog::info("Material {} IsotropicPlastic.{} = {}", mid, param_name, value);
        print_current();
    }
    else if (component_name == "RateDependentPlastic") {
        if (!registry.all_of<::Component::RateDependentPlasticParams>(mat_e)) {
            spdlog::error("Material {} does not have RateDependentPlasticParams.", mid);
            return;
        }
        auto& p = registry.get<::Component::RateDependentPlasticParams>(mat_e);
        if (param_name == "hardening_mode")               p.hardening_mode = value;
        else if (param_name == "failure_plastic_strain")  p.failure_plastic_strain = value;
        else if (param_name == "fail_begin_tensile_strain") p.fail_begin_tensile_strain = value;
        else if (param_name == "fail_end_tensile_strain") p.fail_end_tensile_strain = value;
        else if (param_name == "elem_del_tensile_strain") p.elem_del_tensile_strain = value;
        else { spdlog::error("Unknown RateDependentPlastic param: '{}'.", param_name); return; }
        spdlog::info("Material {} RateDependentPlastic.{} = {}", mid, param_name, value);
        print_current();
    }
    else if (component_name == "HyperelasticMode") {
        if (!registry.all_of<::Component::HyperelasticMode>(mat_e)) {
            spdlog::error("Material {} does not have HyperelasticMode.", mid);
            return;
        }
        auto& p = registry.get<::Component::HyperelasticMode>(mat_e);
        if (param_name == "order")   { p.order = static_cast<int>(value); }
        else if (param_name == "nu") { p.nu = value; }
        else { spdlog::error("Unknown HyperelasticMode param: '{}'. Valid: order, nu", param_name); return; }
        spdlog::info("Material {} HyperelasticMode.{} = {}", mid, param_name, value);
        print_current();
    }
    else {
        spdlog::error("Unknown component: '{}'. Valid: LinearElastic, IsotropicPlastic, RateDependentPlastic, HyperelasticMode", component_name);
        return;
    }
}

// =======================================================
// Section property editing
// =======================================================
void handle_set_section(std::stringstream& ss, AppSession& session) {
    int sid;
    std::string section_type, param_name, value_str;
    if (!(ss >> sid >> section_type >> param_name >> value_str)) {
        spdlog::error("Usage: set_section <section_id> <section_type> <param> <value>");
        spdlog::info("  Section types: Solid, Truss, Shell, SolidAdvanced, SolidShell, SolidShComp, Beam, FiberBeam, Cohesive, AxialSpringDamper, BeamSpring");
        return;
    }

    auto& registry = session.data.registry;
    entt::entity prop_e = find_property_by_id(registry, sid);
    if (prop_e == entt::null) {
        spdlog::error("Section {} not found.", sid);
        return;
    }

    auto parse_bool = [](const std::string& s) -> bool {
        return s == "true" || s == "1" || s == "yes" || s == "on";
    };

    if (section_type == "Solid" || section_type == "SolidAdvanced") {
        if (!registry.all_of<Component::SolidProperty>(prop_e)) {
            spdlog::error("Section {} does not have SolidProperty.", sid);
            return;
        }
        if (param_name == "type_id") {
            registry.get<Component::SolidProperty>(prop_e).type_id = std::stoi(value_str);
        }
        else if (param_name == "integration_network") {
            registry.emplace_or_replace<Component::IntegrationPoints>(prop_e, std::stoi(value_str));
        }
        else if (param_name == "hourglass_control") {
            registry.emplace_or_replace<Component::HourglassControl>(prop_e, value_str);
        }
        else if (param_name == "formulation") {
            registry.emplace_or_replace<Component::Formulation>(prop_e, value_str);
        }
        else if (param_name == "small_strain") {
            registry.emplace_or_replace<Component::SmallStrain>(prop_e, value_str);
        }
        else if (param_name == "const_pressure") {
            registry.emplace_or_replace<Component::ConstPressure>(prop_e, value_str);
        }
        else if (param_name == "co_rotation") {
            registry.emplace_or_replace<Component::CoRotationFlag>(prop_e, value_str);
        }
        else if (param_name == "visco_hourglass_k") {
            registry.emplace_or_replace<Component::ViscoHourglassK>(prop_e, std::stod(value_str));
        }
        else if (param_name == "qa") {
            registry.get_or_emplace<Component::ViscosityParams>(prop_e).quadratic = std::stod(value_str);
        }
        else if (param_name == "qb") {
            registry.get_or_emplace<Component::ViscosityParams>(prop_e).linear = std::stod(value_str);
        }
        else if (param_name == "dtmin") {
            registry.emplace_or_replace<Component::DtMin>(prop_e, std::stod(value_str));
        }
        else if (param_name == "numeric_damping") {
            registry.emplace_or_replace<Component::NumericDamping>(prop_e, std::stod(value_str));
        }
        else if (param_name == "distortion_control") {
            registry.get_or_emplace<Component::DistortionControl>(prop_e).enabled = parse_bool(value_str);
        }
        else if (param_name == "dc0") {
            registry.get_or_emplace<Component::DistortionControl>(prop_e).coeffs[0] = std::stod(value_str);
        }
        else if (param_name == "dc1") {
            registry.get_or_emplace<Component::DistortionControl>(prop_e).coeffs[1] = std::stod(value_str);
        }
        else if (param_name == "dc2") {
            registry.get_or_emplace<Component::DistortionControl>(prop_e).coeffs[2] = std::stod(value_str);
        }
        else if (param_name == "disp_hg_factor") {
            registry.emplace_or_replace<Component::DispHourglassFactor>(prop_e, std::stod(value_str));
        }
        else if (param_name == "ele_charac_length") {
            registry.emplace_or_replace<Component::EleCharacLength>(prop_e, parse_bool(value_str));
        }
        else {
            spdlog::error("Unknown Solid param: '{}'.", param_name);
            return;
        }
    }
    else if (section_type == "Truss") {
        if (!registry.all_of<Component::TrussProperty>(prop_e)) {
            spdlog::error("Section {} does not have TrussProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::TrussProperty>(prop_e);
        if (param_name == "area") { p.area = std::stod(value_str); }
        else {
            spdlog::error("Unknown Truss param: '{}'. Valid: area", param_name);
            return;
        }
    }
    else if (section_type == "Shell") {
        if (!registry.all_of<Component::ShellProperty>(prop_e)) {
            spdlog::error("Section {} does not have ShellProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::ShellProperty>(prop_e);
        if (param_name == "type_id")              { p.type_id = std::stoi(value_str); }
        else if (param_name == "thick0")          { p.thickness[0] = std::stod(value_str); }
        else if (param_name == "thick1")          { p.thickness[1] = std::stod(value_str); }
        else if (param_name == "thick2")          { p.thickness[2] = std::stod(value_str); }
        else if (param_name == "thick3")          { p.thickness[3] = std::stod(value_str); }
        else if (param_name == "thickness_change") { p.thickness_change = parse_bool(value_str); }
        else if (param_name == "drill_dof")       { p.drill_dof = parse_bool(value_str); }
        else if (param_name == "shear_factor")    { p.shear_factor = std::stod(value_str); }
        else if (param_name == "integration_points") { p.integration_points = std::stoi(value_str); }
        else if (param_name == "inp_rule")        { p.inp_rule = value_str; }
        else if (param_name == "fail_thick")      { p.fail_thick = std::stod(value_str); }
        else if (param_name == "hg_hm")           { p.hourglass_coefs[0] = std::stod(value_str); }
        else if (param_name == "hg_hf")           { p.hourglass_coefs[1] = std::stod(value_str); }
        else if (param_name == "hg_hr")           { p.hourglass_coefs[2] = std::stod(value_str); }
        else if (param_name == "plastic_return")  { p.plastic_plane_stress_return = value_str; }
        else if (param_name == "mid_shell_flag")  { p.mid_shell_flag = value_str; }
        else {
            spdlog::error("Unknown Shell param: '{}'.", param_name);
            return;
        }
    }
    else if (section_type == "SolidShell") {
        if (!registry.all_of<Component::SolidShellProperty>(prop_e)) {
            spdlog::error("Section {} does not have SolidShellProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::SolidShellProperty>(prop_e);
        if (param_name == "formulation")         { p.formulation.value = value_str; }
        else if (param_name == "small_strain")   { p.small_strain.value = value_str; }
        else if (param_name == "inpts0")         { p.integration_points[0] = std::stoi(value_str); }
        else if (param_name == "inpts1")         { p.integration_points[1] = std::stoi(value_str); }
        else if (param_name == "inpts2")         { p.integration_points[2] = std::stoi(value_str); }
        else if (param_name == "visco_hourglass_k") { p.visco_hourglass_k = std::stod(value_str); }
        else if (param_name == "qa")             { p.bulk_viscosity.quadratic = std::stod(value_str); }
        else if (param_name == "qb")             { p.bulk_viscosity.linear = std::stod(value_str); }
        else if (param_name == "dtmin")          { p.dtmin = std::stod(value_str); }
        else if (param_name == "thickness_penalty") { p.thickness_penalty = std::stod(value_str); }
        else if (param_name == "dc0")            { p.distortion_coeffs[0] = std::stod(value_str); }
        else if (param_name == "dc1")            { p.distortion_coeffs[1] = std::stod(value_str); }
        else if (param_name == "dc2")            { p.distortion_coeffs[2] = std::stod(value_str); }
        else {
            spdlog::error("Unknown SolidShell param: '{}'.", param_name);
            return;
        }
    }
    else if (section_type == "SolidShComp") {
        if (!registry.all_of<Component::SolidShCompProperty>(prop_e)) {
            spdlog::error("Section {} does not have SolidShCompProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::SolidShCompProperty>(prop_e);
        if (param_name == "formulation")         { p.formulation.value = value_str; }
        else if (param_name == "small_strain")   { p.small_strain.value = value_str; }
        else if (param_name == "inpts0")         { p.integration_points[0] = std::stoi(value_str); }
        else if (param_name == "inpts1")         { p.integration_points[1] = std::stoi(value_str); }
        else if (param_name == "inpts2")         { p.integration_points[2] = std::stoi(value_str); }
        else if (param_name == "numeric_damping") { p.numeric_damping = std::stod(value_str); }
        else if (param_name == "qa")             { p.bulk_viscosity.quadratic = std::stod(value_str); }
        else if (param_name == "qb")             { p.bulk_viscosity.linear = std::stod(value_str); }
        else if (param_name == "thickness_penalty") { p.thickness_penalty = std::stod(value_str); }
        else if (param_name == "coord_sys")      { p.coord_sys.value = value_str; }
        else {
            spdlog::error("Unknown SolidShComp param: '{}'.", param_name);
            return;
        }
    }
    else if (section_type == "Beam") {
        if (!registry.all_of<Component::BeamProperty>(prop_e)) {
            spdlog::error("Section {} does not have BeamProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::BeamProperty>(prop_e);
        if (param_name == "small_strain") { p.small_strain.value = value_str; }
        else if (param_name == "area")    { p.area = std::stod(value_str); }
        else if (param_name == "ixx")     { p.ixx = std::stod(value_str); }
        else if (param_name == "iyy")     { p.iyy = std::stod(value_str); }
        else if (param_name == "izz")     { p.izz = std::stod(value_str); }
        else if (param_name == "shear_flag") { p.shear_flag = parse_bool(value_str); }
        else {
            spdlog::error("Unknown Beam param: '{}'. Valid: small_strain, area, ixx, iyy, izz, shear_flag", param_name);
            return;
        }
    }
    else if (section_type == "FiberBeam") {
        if (!registry.all_of<Component::FiberBeamProperty>(prop_e)) {
            spdlog::error("Section {} does not have FiberBeamProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::FiberBeamProperty>(prop_e);
        if (param_name == "pattern")            { p.pattern = value_str; }
        else if (param_name == "small_strain")  { p.small_strain.value = value_str; }
        else if (param_name == "integration_points") { p.integration_points = std::stoi(value_str); }
        else {
            spdlog::error("Unknown FiberBeam param: '{}'. Valid: pattern, small_strain, integration_points", param_name);
            return;
        }
    }
    else if (section_type == "Cohesive") {
        if (!registry.all_of<Component::CohesiveProperty>(prop_e)) {
            spdlog::error("Section {} does not have CohesiveProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::CohesiveProperty>(prop_e);
        if (param_name == "small_strain") { p.small_strain.value = value_str; }
        else if (param_name == "thickness") { p.thickness = std::stod(value_str); }
        else {
            spdlog::error("Unknown Cohesive param: '{}'. Valid: small_strain, thickness", param_name);
            return;
        }
    }
    else if (section_type == "AxialSpringDamper") {
        if (!registry.all_of<Component::AxialSpringDamperProperty>(prop_e)) {
            spdlog::error("Section {} does not have AxialSpringDamperProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::AxialSpringDamperProperty>(prop_e);
        if (param_name == "mass")               { p.mass = std::stod(value_str); }
        else if (param_name == "stiffness")     { p.stiffness = std::stod(value_str); }
        else if (param_name == "damping")       { p.damping = std::stod(value_str); }
        else if (param_name == "hardening_flag") { p.hardening_flag = value_str; }
        else if (param_name == "nonlinear_spring") { p.nonlinear_spring = parse_bool(value_str); }
        else if (param_name == "nonlinear_damper") { p.nonlinear_damper = parse_bool(value_str); }
        else {
            spdlog::error("Unknown AxialSpringDamper param: '{}'. Valid: mass, stiffness, damping, hardening_flag, nonlinear_spring, nonlinear_damper", param_name);
            return;
        }
    }
    else if (section_type == "BeamSpring") {
        if (!registry.all_of<Component::BeamSpringProperty>(prop_e)) {
            spdlog::error("Section {} does not have BeamSpringProperty.", sid);
            return;
        }
        auto& p = registry.get<Component::BeamSpringProperty>(prop_e);
        if (param_name == "mass")               { p.mass = std::stod(value_str); }
        else if (param_name == "inertia")       { p.inertia = std::stod(value_str); }
        else if (param_name == "coord_sys")     { p.coord_sys.value = value_str; }
        else if (param_name == "ref_tran_vel")  { p.ref_tran_vel = std::stod(value_str); }
        else if (param_name == "ref_rot_vel")   { p.ref_rot_vel = std::stod(value_str); }
        else if (param_name == "smooth_strain_rate") { p.smooth_strain_rate = parse_bool(value_str); }
        else if (param_name == "failure_criteria")  { p.failure_criteria = value_str; }
        else if (param_name == "length_flag")       { p.length_flag = value_str; }
        else if (param_name == "failure_model")     { p.failure_model = value_str; }
        else if (param_name == "k0") { p.linear_stiffness[0] = std::stod(value_str); }
        else if (param_name == "k1") { p.linear_stiffness[1] = std::stod(value_str); }
        else if (param_name == "k2") { p.linear_stiffness[2] = std::stod(value_str); }
        else if (param_name == "k3") { p.linear_stiffness[3] = std::stod(value_str); }
        else if (param_name == "k4") { p.linear_stiffness[4] = std::stod(value_str); }
        else if (param_name == "k5") { p.linear_stiffness[5] = std::stod(value_str); }
        else if (param_name == "c0") { p.linear_damping[0] = std::stod(value_str); }
        else if (param_name == "c1") { p.linear_damping[1] = std::stod(value_str); }
        else if (param_name == "c2") { p.linear_damping[2] = std::stod(value_str); }
        else if (param_name == "c3") { p.linear_damping[3] = std::stod(value_str); }
        else if (param_name == "c4") { p.linear_damping[4] = std::stod(value_str); }
        else if (param_name == "c5") { p.linear_damping[5] = std::stod(value_str); }
        else {
            spdlog::error("Unknown BeamSpring param: '{}'.", param_name);
            return;
        }
    }
    else {
        spdlog::error("Unknown section type: '{}'. Valid: Solid, Truss, Shell, SolidAdvanced, SolidShell, SolidShComp, Beam, FiberBeam, Cohesive, AxialSpringDamper, BeamSpring", section_type);
        return;
    }

    spdlog::info("Section {} {}.{} = {}", sid, section_type, param_name, value_str);
}

} // namespace cmd::property
