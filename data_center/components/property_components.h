// data_center/components/property_components.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#pragma once

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <entt/entt.hpp>

/**
 * @namespace Component
 * @brief ECS components - Property (section properties) section
 * @details Property is decoupled from Material, storing only section and integration related parameters (integration scheme, hourglass control, etc.).
 * Material is bound through SimdroidPart, see simdroid_components.h.
 */
namespace Component {

    /**
     * @brief [New] Attached to Property entity, stores its user-defined ID (pid)
     * @details Used to identify Property entity, avoiding ID conflicts with other types of entities
     */
    struct PropertyID {
        int value;
    };

    /**
     * @brief [New] Attached to Property entity, marker for solid elements.
     * @details Only retains type_id to preserve the property subtype (solid / solid_orthotropic, etc.).
     *          All other solid parameters are stored as independent components (IntegrationPoints,
     *          HourglassControl, Formulation, SmallStrain, etc.) so that different solvers can
     *          freely compose the subset they need.
     */
    struct SolidProperty {
        int type_id;  // From JSON "typeid"
    };

    // ------------------------------------------------------------------
    //  Common small components (reusable by multiple Property types)
    // ------------------------------------------------------------------

    // Element formulation/recipe (e.g. "Shell4", "Hex8R", etc.)
    struct Type{
        std::string value;
    };
    struct Formulation {
        std::string value;
    };

    // Small strain options ("Auto", "T0", "Tnone", etc.)
    struct SmallStrain {
        std::string value;
    };

    // Bulk/hourglass viscosity parameters (qa, qb)
    struct ViscosityParams {
        double quadratic = 0.1; // qa
        double linear    = 0.05; // qb
    };

    // General local coordinate system marker
    struct CoordSys {
        std::string value;
    };

    // ------------------------------------------------------------------
    //  Solid property sub-components (independent, composable)
    //  These replace the former SolidAdvancedProperty monolith so that
    //  different solvers (Simdroid / Abaqus / JSON) can attach only the
    //  parameters they actually need.
    // ------------------------------------------------------------------

    // Number of integration points (e.g. 1 for reduced, 2/8/27 for full)
    struct IntegrationPoints {
        int value = 1;
    };

    // Hourglass control method ("eas", "rph", "enhanced", "stiffness",
    // "relax_stiffness", "standard", "viscous", "" = none)
    // Merges the former "hourglass_control" and "hourglass_type" fields.
    struct HourglassControl {
        std::string value;
    };

    // Constant pressure assumption (Simdroid Icpre)
    struct ConstPressure {
        std::string value;
    };

    // Co-rotation frame flag (Simdroid Iframe)
    struct CoRotationFlag {
        std::string value;
    };

    // Viscous hourglass coefficient h
    struct ViscoHourglassK {
        double value = 0.1;
    };

    // Minimum time step (dtmin)
    struct DtMin {
        double value = 0.0;
    };

    // Numeric damping (dn)
    struct NumericDamping {
        double value = 0.0;
    };

    // Distortion control (enabled + 3 coefficients)
    struct DistortionControl {
        bool enabled = false;
        std::array<double, 3> coeffs{};
    };

    // Displacement hourglass factor
    struct DispHourglassFactor {
        double value = 1.0;
    };

    // Element characteristic length flag
    struct EleCharacLength {
        bool value = false;
    };

    // ------------------------------------------------------------------
    //  Typical section property components (mapped to Simdroid CrossSection / Radioss PROP)
    // ------------------------------------------------------------------

    // Type == Truss (truss)
    struct TrussProperty {
        double area = 0.0; // Area
    };

    // Type == Shell / SandwichShell
    struct ShellProperty {
        int   type_id = 0;                         // Spare: Shell / Sandwich / Sh3n ...
        std::array<double, 4> thickness{};        // Thick[4]
        bool  thickness_change = false;           // Ithick
        bool  drill_dof        = false;           // Idrill
        double shear_factor    = 0.83;            // Ashear
        int   integration_points = 2;             // InpNum / N
        std::string inp_rule;                     // InpRule (e.g. "Gauss")
        double fail_thick = 1.0;                  // P_thickfail
        std::array<double, 3> hourglass_coefs{};  // hm, hf, hr
        std::string plastic_plane_stress_return;  // "Default" / "Iteration" / "Newton"
        std::string mid_shell_flag;               // "NoOffset"/"Upper"/"Lower"
    };

    // Type == SolidShell (PROP TYPE20)
    struct SolidShellProperty {
        Formulation formulation;              // TShell / TShellRPH
        SmallStrain small_strain;            // Ismstr
        std::array<int, 3> integration_points{}; // Inpts (r,s,t)
        double visco_hourglass_k = 0.0;      // h
        ViscosityParams bulk_viscosity;      // qa / qb
        double dtmin = 0.0;                  // Δtmin
        double thickness_penalty = 10.0;     // ThicknessPenaltyFactor
        std::array<double, 3> distortion_coeffs{}; // DistortionControlCoeffs
    };

    // Type == SolidShComp (composite SolidShell, PROP TYPE22)
    struct SolidShCompProperty {
        Formulation formulation;              // TShell / TShellRPH
        SmallStrain small_strain;            // Ismstr
        std::array<int, 3> integration_points{}; // Inpts
        double numeric_damping = 0.0;        // dn
        ViscosityParams bulk_viscosity;      // qa / qb
        double thickness_penalty = 10.0;     // ThicknessPenaltyFactor
        CoordSys coord_sys;                  // skew_ID

        // Composite layer data
        std::vector<double> layer_angles;    // Angles[]
        std::vector<double> layer_thicks;    // Thicks[]
        std::vector<double> layer_positions; // Positions[]
        std::vector<std::string> layer_materials; // Materials[]
        std::vector<std::string> position_flags;  // Ipos[]
    };

    // Type == GeneralBeam (TYPE3)
    struct BeamProperty {
        SmallStrain small_strain;  // Ismstr
        double area = 0.0;         // Area
        double ixx  = 0.0;         // IXX
        double iyy  = 0.0;         // IYY
        double izz  = 0.0;         // IZZ
        bool shear_flag = true;    // Ishear (true/false)
    };

    // Type == FiberBeam (TYPE18)
    struct FiberBeamProperty {
        std::string pattern;             // Pattern
        SmallStrain small_strain;        // Ismstr
        int integration_points = 0;      // InpNum
        std::vector<double> yi;          // Yi[]
        std::vector<double> zi;          // Zi[]
        std::vector<double> areai;       // Areai[]
        std::vector<double> dj;          // Dj[...] section dimensions D*
    };

    // Type == Cohesive (TYPE43)
    struct CohesiveProperty {
        SmallStrain small_strain; // Ismstr
        double thickness = 0.0;   // True_thickness
    };

    // Type == AxialSpringDamper (TYPE4)
    struct AxialSpringDamperProperty {
        double mass       = 0.0;  // Mass
        double stiffness  = 0.0;  // K1
        double damping    = 0.0;  // C1 (DampingCoefficient)

        bool nonlinear_spring = false;   // NonlinearSpring
        bool nonlinear_damper = false;   // NonlinearDamper
        std::string hardening_flag;      // HardeningFlag

        // Curve references (resolved via DofMap's curve_name_to_entity)
        entt::entity stiffness_curve = entt::null; // Load_DeflectionCurve
        entt::entity damping_curve   = entt::null; // DampingCurve
    };

    // Type == BeamSpring (TYPE13)
    struct BeamSpringProperty {
        double mass    = 0.0;  // Mass
        double inertia = 0.0;  // Inertia
        CoordSys coord_sys;    // skew_ID

        std::string failure_criteria; // FailureCriteria
        std::string length_flag;      // LengthFlag
        std::string failure_model;    // FailureModel

        std::array<double, 6> linear_stiffness{};   // K
        std::array<double, 6> linear_damping{};     // C
        std::array<double, 6> non_stiff_fac_a{};    // A
        std::array<double, 6> non_stiff_fac_b{};    // B
        std::array<double, 6> non_stiff_fac_d{};    // D

        std::array<std::string, 6> hardening_flag{}; // HardenFlag[6]

        // 6-direction curve references
        std::array<entt::entity, 6> nonlinear_stiffness{};      // f(δ)
        std::array<entt::entity, 6> for_or_mom_with_vel{};      // g(δ)
        std::array<entt::entity, 6> harden_related_curve{};     // according to HardenFlag
        std::array<entt::entity, 6> nonlinear_damping{};        // h(δ)

        std::array<double, 6> upper_failure_limit{}; // δmax / θmax
        std::array<double, 6> lower_failure_limit{}; // δmin / θmin

        std::array<double, 6> absc_scale_damp{};     // F
        std::array<double, 6> ordina_scale_damp{};   // H
        std::array<double, 6> absc_scale_stiff{};    // Ascale
        std::array<double, 6> ordina_scale_stiff{};  // (unnamed coefficient)

        double ref_tran_vel = 0.0; // RefTranVel
        double ref_rot_vel  = 0.0; // RefRotVel

        bool smooth_strain_rate = false; // SmoothStrRate
        std::array<double, 6> rela_vec_coeff{};      // RelaVecCoeff
        std::array<double, 6> rela_vec_exp{};        // RelaVecExp
        std::array<double, 6> failure_scale{};       // FailureScale
        std::array<double, 6> failure_exp{};         // FailureExp
    };

    // ------------------------------------------------------------------
    //  Solid formulation → (IntegrationPoints, HourglassControl) lookup
    // ------------------------------------------------------------------

    /// Simdroid solid formulation → (integration points, hourglass control)
    inline std::pair<int, std::string> simdroid_solid_formulation_lookup(
        const std::string& formulation)
    {
        static const std::unordered_map<std::string, std::pair<int, std::string>> table = {
            {"Hex8",      {2, ""}},      // full integration, no hourglass
            {"Hex8RPH",   {1, "rph"}},
            {"Hex8r_EAS", {1, "EAS"}},
            {"Tet4",      {1, ""}},
        };
        auto it = table.find(formulation);
        if (it != table.end()) return it->second;
        return {1, ""};  // default fallback
    }

    /// Abaqus solid element type string → (integration points, hourglass control)
    /// Values based on standard Abaqus defaults; adjust as needed.
    inline std::pair<int, std::string> abaqus_solid_element_lookup(
        const std::string& element_type)
    {
        // Normalise to upper-case for matching
        std::string t = element_type;
        std::transform(t.begin(), t.end(), t.begin(),
            [](unsigned char c) { return std::toupper(c); });

        static const std::unordered_map<std::string, std::pair<int, std::string>> table = {
            {"C3D8",    {8, ""}},            // full integration hex8
            {"C3D8R",   {1, "enhanced"}},    // reduced integration hex8
            {"C3D8I",   {8, ""}},            // incompatible modes hex8
            {"C3D8H",   {8, ""}},            // hybrid hex8
            {"C3D4",    {1, ""}},            // linear tet
            {"C3D10",   {4, ""}},            // quadratic tet
            {"C3D10M",  {4, ""}},            // modified quadratic tet
            {"C3D10H",  {4, ""}},            // hybrid quadratic tet
            {"C3D10I",  {4, ""}},            // incompatible quadratic tet
            {"C3D20",   {27, ""}},           // full integration hex20
            {"C3D20R",  {8, "enhanced"}},    // reduced integration hex20
            {"C3D20H",  {27, ""}},           // hybrid hex20
            {"C3D6",    {2, ""}},            // wedge
            {"C3D6R",   {1, "enhanced"}},    // reduced wedge
        };
        auto it = table.find(t);
        if (it != table.end()) return it->second;
        return {1, ""};  // default fallback
    }

} // namespace Component

