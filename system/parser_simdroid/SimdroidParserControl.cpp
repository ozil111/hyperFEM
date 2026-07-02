// ============================================================
// SimdroidParserControl.cpp
//
// Control JSON parsing: Function/Curve pre-parsing,
// CrossSection, Material, PartProperty, Contact sub-parsers,
// and the main parse_control_json() dispatcher.
// ============================================================

#include "SimdroidParser.h"
#include "SimdroidParserDetail.h"

#include "../../data_center/DofMap.h"

#include "nlohmann/json.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SimdroidParserDetail;
using json = nlohmann::json;

// ---------------------------------------------------------------
// Curve entity helpers (extracted from lambdas in parse_control_json)
// ---------------------------------------------------------------

entt::entity SimdroidParser::get_or_create_curve_entity(
    const std::string& fname, entt::registry& registry,
    DofMap* dof_map, const std::filesystem::path& control_dir)
{
    // Already exists -> return directly
    auto it = dof_map->curve_name_to_entity.find(fname);
    if (it != dof_map->curve_name_to_entity.end()) {
        return it->second;
    }

    // Create new Curve entity
    entt::entity curve_e = registry.create();

    // CurveID is for debug/distinction; use mapping size as simple ID
    int cid = static_cast<int>(dof_map->curve_name_to_entity.size());
    registry.emplace<Component::CurveID>(curve_e, cid);

    Component::Curve curve{};
    curve.type = "tabular"; // generic 1D data points

    // Read data from same-name .txt file: col1=strain(x), col2=stress(y), skip '#' lines
    std::filesystem::path txt_path = control_dir / (fname + ".txt");
    std::ifstream fin(txt_path);
    if (!fin.is_open()) {
        spdlog::warn("Function '{}' expects txt file '{}', but it cannot be opened.", fname, txt_path.string());
    } else {
        std::string line;
        while (std::getline(fin, line)) {
            if (line.empty()) continue;
            if (!line.empty() && line[0] == '#') continue;

            std::istringstream iss(line);
            double col1 = 0.0, col2 = 0.0;
            if (!(iss >> col1 >> col2)) continue;

            // convention: x=strain, y=stress
            curve.x.push_back(col1);
            curve.y.push_back(col2);
        }
        if (curve.x.empty()) {
            spdlog::warn("Txt file '{}' for Function '{}' contains no valid data rows.", txt_path.string(), fname);
        }
    }

    registry.emplace<Component::Curve>(curve_e, std::move(curve));
    dof_map->curve_name_to_entity.emplace(fname, curve_e);
    dof_map->curve_entity_to_name.emplace(curve_e, fname);

    return curve_e;
}

entt::entity SimdroidParser::resolve_curve_entity(
    const std::string& raw_name, entt::registry& registry, DofMap* dof_map)
{
    if (!dof_map) return entt::null;
    std::string fname = raw_name;
    trim(fname);
    if (fname.empty()) return entt::null;
    auto it = dof_map->curve_name_to_entity.find(fname);
    if (it != dof_map->curve_name_to_entity.end()) {
        return it->second;
    }
    return get_or_create_curve_entity(fname, registry, dof_map, std::filesystem::path());
}

// ---------------------------------------------------------------
// Sub-parser: CrossSection / Property definitions
// ---------------------------------------------------------------

void SimdroidParser::parse_cross_sections(
    const nlohmann::json& j, entt::registry& registry,
    std::unordered_map<std::string, entt::entity>& cross_section_map,
    DofMap* dof_map)
{
    if (!j.contains("CrossSection") || !j["CrossSection"].is_object()) return;

    int pid_counter = 1; // Auto-incrementing PID

    for (auto& [cs_name, cs_val] : j["CrossSection"].items()) {
        const entt::entity cs_entity = registry.create();
        registry.emplace<Component::SetName>(cs_entity, cs_name);
        registry.emplace<Component::PropertyID>(cs_entity, pid_counter++);
        cross_section_map.emplace(cs_name, cs_entity);

        const std::string type_str = cs_val.value("Type", "");
        const std::string type_l = to_lower_copy(type_str);

        // Register Type component for all CrossSection entities
        if (!type_str.empty()) {
            registry.emplace<Component::Type>(cs_entity, type_str);
        }

        // --- Solid / SolidOrthotropic ---
        if (type_l == "solid" || type_l == "solidorthotropic") {
            // Marker + type_id
            registry.emplace<Component::SolidProperty>(cs_entity, 1);

            // Formulation
            std::string formulation;
            if (cs_val.contains("Formulation") && cs_val["Formulation"].is_array() && !cs_val["Formulation"].empty()) {
                formulation = cs_val["Formulation"].front().get<std::string>();
            }
            if (!formulation.empty()) {
                registry.emplace<Component::Formulation>(cs_entity, formulation);
            }

            // Small strain
            std::string small_strain = cs_val.value("SmallStrain", "");
            if (!small_strain.empty()) {
                registry.emplace<Component::SmallStrain>(cs_entity, small_strain);
            }

            // Const pressure
            std::string const_pressure = cs_val.value("ConstPressure", "");
            if (!const_pressure.empty()) {
                registry.emplace<Component::ConstPressure>(cs_entity, const_pressure);
            }

            // Co-rotation flag
            std::string co_rotation = cs_val.value("CoRotationFlag", "");
            if (!co_rotation.empty()) {
                registry.emplace<Component::CoRotationFlag>(cs_entity, co_rotation);
            }

            // ViscoHourglassK
            double visco_hg_k = 0.1;
            if (cs_val.contains("ViscoHourglassK")) {
                visco_hg_k = cs_val["ViscoHourglassK"].get<double>();
            } else if (cs_val.contains("hm")) {
                visco_hg_k = cs_val["hm"].get<double>();
            }
            registry.emplace<Component::ViscoHourglassK>(cs_entity, visco_hg_k);

            // Viscosity params
            {
                Component::ViscosityParams vp;
                if (cs_val.contains("QuadraticViscosity")) {
                    vp.quadratic = cs_val["QuadraticViscosity"].get<double>();
                }
                if (cs_val.contains("LinearViscosity")) {
                    vp.linear = cs_val["LinearViscosity"].get<double>();
                }
                registry.emplace<Component::ViscosityParams>(cs_entity, vp);
            }

            // dtmin
            if (cs_val.contains("dtmin") && cs_val["dtmin"].is_number()) {
                registry.emplace<Component::DtMin>(cs_entity, cs_val["dtmin"].get<double>());
            }

            // Numeric damping
            if (cs_val.contains("DampingNumeri") && cs_val["DampingNumeri"].is_number()) {
                registry.emplace<Component::NumericDamping>(cs_entity, cs_val["DampingNumeri"].get<double>());
            }

            // Distortion control
            if (cs_val.contains("DistortionControl") && cs_val["DistortionControl"].is_boolean()) {
                Component::DistortionControl dc;
                dc.enabled = cs_val["DistortionControl"].get<bool>();
                if (cs_val.contains("DistortionControlCoeffs") && cs_val["DistortionControlCoeffs"].is_array()) {
                    const auto& arr = cs_val["DistortionControlCoeffs"];
                    for (size_t i = 0; i < std::min<std::size_t>(3, arr.size()); ++i) {
                        dc.coeffs[i] = arr[i].get<double>();
                    }
                }
                registry.emplace<Component::DistortionControl>(cs_entity, std::move(dc));
            }

            // Disp hourglass factor
            if (cs_val.contains("DispHourglassFactor") && cs_val["DispHourglassFactor"].is_number()) {
                registry.emplace<Component::DispHourglassFactor>(cs_entity, cs_val["DispHourglassFactor"].get<double>());
            }

            // Element characteristic length
            if (cs_val.contains("EleCharacLength") && cs_val["EleCharacLength"].is_boolean()) {
                registry.emplace<Component::EleCharacLength>(cs_entity, cs_val["EleCharacLength"].get<bool>());
            }

            // Auto-fill IntegrationPoints and HourglassControl from formulation lookup.
            // Explicit HourglassType (if present) overrides the lookup default.
            auto [npts, hg] = Component::simdroid_solid_formulation_lookup(formulation);
            std::string hourglass_type = cs_val.value("HourglassType", "");
            if (!hourglass_type.empty()) hg = hourglass_type;
            registry.emplace<Component::IntegrationPoints>(cs_entity, npts);
            registry.emplace<Component::HourglassControl>(cs_entity, hg);
        }
        // --- Shell / SandwichShell ---
        else if (type_l == "shell" || type_l == "sandwichshell") {
            Component::ShellProperty prop{};
            prop.type_id = (type_l == "shell") ? 1 : 11;

            if (cs_val.contains("Thickness") && cs_val["Thickness"].is_array()) {
                const auto& arr = cs_val["Thickness"];
                for (size_t i = 0; i < std::min<std::size_t>(4, arr.size()); ++i) {
                    prop.thickness[i] = arr[i].get<double>();
                }
            }
            if (cs_val.contains("ThicknessChange") && cs_val["ThicknessChange"].is_boolean()) {
                prop.thickness_change = cs_val["ThicknessChange"].get<bool>();
            }
            if (cs_val.contains("DrillDof") && cs_val["DrillDof"].is_boolean()) {
                prop.drill_dof = cs_val["DrillDof"].get<bool>();
            }
            if (cs_val.contains("ShearFactor") && cs_val["ShearFactor"].is_number()) {
                prop.shear_factor = cs_val["ShearFactor"].get<double>();
            }
            if (cs_val.contains("InpNum") && cs_val["InpNum"].is_number_integer()) {
                prop.integration_points = cs_val["InpNum"].get<int>();
            }
            if (cs_val.contains("InpRule") && cs_val["InpRule"].is_string()) {
                prop.inp_rule = cs_val["InpRule"].get<std::string>();
            }
            if (cs_val.contains("FailThick") && cs_val["FailThick"].is_number()) {
                prop.fail_thick = cs_val["FailThick"].get<double>();
            }
            if (cs_val.contains("HourglassCofs") && cs_val["HourglassCofs"].is_array()) {
                const auto& arr = cs_val["HourglassCofs"];
                for (size_t i = 0; i < std::min<std::size_t>(3, arr.size()); ++i) {
                    prop.hourglass_coefs[i] = arr[i].get<double>();
                }
            }
            if (cs_val.contains("PlasticPlaneStressReturn") && cs_val["PlasticPlaneStressReturn"].is_string()) {
                prop.plastic_plane_stress_return = cs_val["PlasticPlaneStressReturn"].get<std::string>();
            }
            if (cs_val.contains("MidShellFlag") && cs_val["MidShellFlag"].is_string()) {
                prop.mid_shell_flag = cs_val["MidShellFlag"].get<std::string>();
            }

            registry.emplace<Component::ShellProperty>(cs_entity, std::move(prop));
        }
        // --- SolidShell / SolidShComp (thick shell / composite thick shell) ---
        else if (type_l == "solidshell") {
            Component::SolidShellProperty prop{};
            if (cs_val.contains("Formulation") && cs_val["Formulation"].is_string()) {
                prop.formulation.value = cs_val["Formulation"].get<std::string>();
            }
            if (cs_val.contains("SmallStrain") && cs_val["SmallStrain"].is_string()) {
                prop.small_strain.value = cs_val["SmallStrain"].get<std::string>();
            }
            if (cs_val.contains("InpNum") && cs_val["InpNum"].is_array()) {
                const auto& arr = cs_val["InpNum"];
                for (size_t i = 0; i < std::min<std::size_t>(3, arr.size()); ++i) {
                    prop.integration_points[i] = arr[i].get<int>();
                }
            }
            if (cs_val.contains("ViscoHourglassK") && cs_val["ViscoHourglassK"].is_number()) {
                prop.visco_hourglass_k = cs_val["ViscoHourglassK"].get<double>();
            }
            if (cs_val.contains("QuadraticViscosity") && cs_val["QuadraticViscosity"].is_number()) {
                prop.bulk_viscosity.quadratic = cs_val["QuadraticViscosity"].get<double>();
            }
            if (cs_val.contains("LinearViscosity") && cs_val["LinearViscosity"].is_number()) {
                prop.bulk_viscosity.linear = cs_val["LinearViscosity"].get<double>();
            }
            if (cs_val.contains("dtmin") && cs_val["dtmin"].is_number()) {
                prop.dtmin = cs_val["dtmin"].get<double>();
            }
            if (cs_val.contains("ThicknessPenaltyFactor") && cs_val["ThicknessPenaltyFactor"].is_number()) {
                prop.thickness_penalty = cs_val["ThicknessPenaltyFactor"].get<double>();
            }
            if (cs_val.contains("DistortionControlCoeffs") && cs_val["DistortionControlCoeffs"].is_array()) {
                const auto& arr = cs_val["DistortionControlCoeffs"];
                for (size_t i = 0; i < std::min<std::size_t>(3, arr.size()); ++i) {
                    prop.distortion_coeffs[i] = arr[i].get<double>();
                }
            }
            registry.emplace<Component::SolidShellProperty>(cs_entity, std::move(prop));
        }
        else if (type_l == "solidshcomp") {
            Component::SolidShCompProperty prop{};
            if (cs_val.contains("Formulation") && cs_val["Formulation"].is_string()) {
                prop.formulation.value = cs_val["Formulation"].get<std::string>();
            }
            if (cs_val.contains("SmallStrain") && cs_val["SmallStrain"].is_string()) {
                prop.small_strain.value = cs_val["SmallStrain"].get<std::string>();
            }
            if (cs_val.contains("InpNum") && cs_val["InpNum"].is_array()) {
                const auto& arr = cs_val["InpNum"];
                for (size_t i = 0; i < std::min<std::size_t>(3, arr.size()); ++i) {
                    prop.integration_points[i] = arr[i].get<int>();
                }
            }
            if (cs_val.contains("DampingNumeri") && cs_val["DampingNumeri"].is_number()) {
                prop.numeric_damping = cs_val["DampingNumeri"].get<double>();
            }
            if (cs_val.contains("QuadraticViscosity") && cs_val["QuadraticViscosity"].is_number()) {
                prop.bulk_viscosity.quadratic = cs_val["QuadraticViscosity"].get<double>();
            }
            if (cs_val.contains("LinearViscosity") && cs_val["LinearViscosity"].is_number()) {
                prop.bulk_viscosity.linear = cs_val["LinearViscosity"].get<double>();
            }
            if (cs_val.contains("ThicknessPenaltyFactor") && cs_val["ThicknessPenaltyFactor"].is_number()) {
                prop.thickness_penalty = cs_val["ThicknessPenaltyFactor"].get<double>();
            }
            if (cs_val.contains("CoordSys") && cs_val["CoordSys"].is_string()) {
                prop.coord_sys.value = cs_val["CoordSys"].get<std::string>();
            }
            if (cs_val.contains("Angles") && cs_val["Angles"].is_array()) {
                prop.layer_angles = cs_val["Angles"].get<std::vector<double>>();
            }
            if (cs_val.contains("Thicks") && cs_val["Thicks"].is_array()) {
                prop.layer_thicks = cs_val["Thicks"].get<std::vector<double>>();
            }
            if (cs_val.contains("Positions") && cs_val["Positions"].is_array()) {
                prop.layer_positions = cs_val["Positions"].get<std::vector<double>>();
            }
            if (cs_val.contains("Materials") && cs_val["Materials"].is_array()) {
                prop.layer_materials = cs_val["Materials"].get<std::vector<std::string>>();
            }
            if (cs_val.contains("PositionFlag") && cs_val["PositionFlag"].is_array()) {
                prop.position_flags = cs_val["PositionFlag"].get<std::vector<std::string>>();
            }
            registry.emplace<Component::SolidShCompProperty>(cs_entity, std::move(prop));
        }
        // --- Beam / FiberBeam / Cohesive / Springs ---
        else if (type_l == "generalbeam" || type_l == "beam") {
            Component::BeamProperty prop{};
            if (cs_val.contains("SmallStrain") && cs_val["SmallStrain"].is_string()) {
                prop.small_strain.value = cs_val["SmallStrain"].get<std::string>();
            }
            if (cs_val.contains("Area") && cs_val["Area"].is_number()) {
                prop.area = cs_val["Area"].get<double>();
            }
            if (cs_val.contains("Ixx") && cs_val["Ixx"].is_number()) {
                prop.ixx = cs_val["Ixx"].get<double>();
            }
            if (cs_val.contains("Iyy") && cs_val["Iyy"].is_number()) {
                prop.iyy = cs_val["Iyy"].get<double>();
            }
            if (cs_val.contains("Izz") && cs_val["Izz"].is_number()) {
                prop.izz = cs_val["Izz"].get<double>();
            }
            if (cs_val.contains("ShearFlag") && cs_val["ShearFlag"].is_boolean()) {
                prop.shear_flag = cs_val["ShearFlag"].get<bool>();
            }
            registry.emplace<Component::BeamProperty>(cs_entity, std::move(prop));
        }
        else if (type_l == "fiberbeam") {
            Component::FiberBeamProperty prop{};
            if (cs_val.contains("Pattern") && cs_val["Pattern"].is_string()) {
                prop.pattern = cs_val["Pattern"].get<std::string>();
            }
            if (cs_val.contains("SmallStrain") && cs_val["SmallStrain"].is_string()) {
                prop.small_strain.value = cs_val["SmallStrain"].get<std::string>();
            }
            if (cs_val.contains("InpNum") && cs_val["InpNum"].is_number_integer()) {
                prop.integration_points = cs_val["InpNum"].get<int>();
            }
            if (cs_val.contains("Yi") && cs_val["Yi"].is_array()) {
                prop.yi = cs_val["Yi"].get<std::vector<double>>();
            }
            if (cs_val.contains("Zi") && cs_val["Zi"].is_array()) {
                prop.zi = cs_val["Zi"].get<std::vector<double>>();
            }
            if (cs_val.contains("Areai") && cs_val["Areai"].is_array()) {
                prop.areai = cs_val["Areai"].get<std::vector<double>>();
            }
            if (cs_val.contains("Dj") && cs_val["Dj"].is_array()) {
                prop.dj = cs_val["Dj"].get<std::vector<double>>();
            }
            registry.emplace<Component::FiberBeamProperty>(cs_entity, std::move(prop));
        }
        else if (type_l == "cohesive") {
            Component::CohesiveProperty prop{};
            if (cs_val.contains("SmallStrain") && cs_val["SmallStrain"].is_string()) {
                prop.small_strain.value = cs_val["SmallStrain"].get<std::string>();
            }
            if (cs_val.contains("Thickness") && cs_val["Thickness"].is_number()) {
                prop.thickness = cs_val["Thickness"].get<double>();
            }
            registry.emplace<Component::CohesiveProperty>(cs_entity, std::move(prop));
        }
        else if (type_l == "axialspringdamper") {
            Component::AxialSpringDamperProperty prop{};
            if (cs_val.contains("Mass") && cs_val["Mass"].is_number()) {
                prop.mass = cs_val["Mass"].get<double>();
            }
            if (cs_val.contains("Stiffness") && cs_val["Stiffness"].is_number()) {
                prop.stiffness = cs_val["Stiffness"].get<double>();
            }
            if (cs_val.contains("DampingCoefficient") && cs_val["DampingCoefficient"].is_number()) {
                prop.damping = cs_val["DampingCoefficient"].get<double>();
            }
            if (cs_val.contains("NonlinearSpring") && cs_val["NonlinearSpring"].is_boolean()) {
                prop.nonlinear_spring = cs_val["NonlinearSpring"].get<bool>();
            }
            if (cs_val.contains("NonlinearDamper") && cs_val["NonlinearDamper"].is_boolean()) {
                prop.nonlinear_damper = cs_val["NonlinearDamper"].get<bool>();
            }
            if (cs_val.contains("HardeningFlag") && cs_val["HardeningFlag"].is_string()) {
                prop.hardening_flag = cs_val["HardeningFlag"].get<std::string>();
            }
            if (cs_val.contains("Load_DeflectionCurve") && cs_val["Load_DeflectionCurve"].is_string()) {
                prop.stiffness_curve = resolve_curve_entity(cs_val["Load_DeflectionCurve"].get<std::string>(), registry, dof_map);
            }
            if (cs_val.contains("DampingCurve") && cs_val["DampingCurve"].is_string()) {
                prop.damping_curve = resolve_curve_entity(cs_val["DampingCurve"].get<std::string>(), registry, dof_map);
            }
            registry.emplace<Component::AxialSpringDamperProperty>(cs_entity, std::move(prop));
        }
        else if (type_l == "beamspring") {
            Component::BeamSpringProperty prop{};
            if (cs_val.contains("Mass") && cs_val["Mass"].is_number()) {
                prop.mass = cs_val["Mass"].get<double>();
            }
            if (cs_val.contains("Inertia") && cs_val["Inertia"].is_number()) {
                prop.inertia = cs_val["Inertia"].get<double>();
            }
            if (cs_val.contains("CoordSys") && cs_val["CoordSys"].is_string()) {
                prop.coord_sys.value = cs_val["CoordSys"].get<std::string>();
            }
            if (cs_val.contains("FailureCriteria") && cs_val["FailureCriteria"].is_string()) {
                prop.failure_criteria = cs_val["FailureCriteria"].get<std::string>();
            }
            if (cs_val.contains("LengthFlag") && cs_val["LengthFlag"].is_string()) {
                prop.length_flag = cs_val["LengthFlag"].get<std::string>();
            }
            if (cs_val.contains("FailureModel") && cs_val["FailureModel"].is_string()) {
                prop.failure_model = cs_val["FailureModel"].get<std::string>();
            }

            auto fill_array_double = [&](const char* key, std::array<double, 6>& dst) {
                if (!cs_val.contains(key) || !cs_val[key].is_array()) return;
                const auto& arr = cs_val[key];
                for (size_t i = 0; i < std::min<std::size_t>(6, arr.size()); ++i) {
                    dst[i] = arr[i].get<double>();
                }
            };
            auto fill_array_string = [&](const char* key, std::array<std::string, 6>& dst) {
                if (!cs_val.contains(key) || !cs_val[key].is_array()) return;
                const auto& arr = cs_val[key];
                for (size_t i = 0; i < std::min<std::size_t>(6, arr.size()); ++i) {
                    if (arr[i].is_string()) dst[i] = arr[i].get<std::string>();
                }
            };
            auto fill_array_curve = [&](const char* key, std::array<entt::entity, 6>& dst) {
                if (!cs_val.contains(key) || !cs_val[key].is_array()) return;
                const auto& arr = cs_val[key];
                for (size_t i = 0; i < std::min<std::size_t>(6, arr.size()); ++i) {
                    if (arr[i].is_string()) {
                        dst[i] = resolve_curve_entity(arr[i].get<std::string>(), registry, dof_map);
                    }
                }
            };

            fill_array_double("LinearStiffness", prop.linear_stiffness);
            fill_array_double("LinearDamping", prop.linear_damping);
            fill_array_double("NonStiffFacA", prop.non_stiff_fac_a);
            fill_array_double("NonStiffFacB", prop.non_stiff_fac_b);
            fill_array_double("NonStiffFacD", prop.non_stiff_fac_d);
            fill_array_string("HardeningFlag", prop.hardening_flag);

            fill_array_curve("NonlinearStiffness", prop.nonlinear_stiffness);
            fill_array_curve("ForOrMomWithVelCurve", prop.for_or_mom_with_vel);
            fill_array_curve("HardenRelatedCurve", prop.harden_related_curve);
            fill_array_curve("NonlinearDamping", prop.nonlinear_damping);

            fill_array_double("UpperFailureLimit", prop.upper_failure_limit);
            fill_array_double("LowerFailureLimit", prop.lower_failure_limit);
            fill_array_double("AbscScaleDamp", prop.absc_scale_damp);
            fill_array_double("OrdinaScaleDamp", prop.ordina_scale_damp);
            fill_array_double("AbscScaleStiff", prop.absc_scale_stiff);
            fill_array_double("OrdinaScaleStiff", prop.ordina_scale_stiff);

            if (cs_val.contains("RefTranVel") && cs_val["RefTranVel"].is_number()) {
                prop.ref_tran_vel = cs_val["RefTranVel"].get<double>();
            }
            if (cs_val.contains("RefRotVel") && cs_val["RefRotVel"].is_number()) {
                prop.ref_rot_vel = cs_val["RefRotVel"].get<double>();
            }
            if (cs_val.contains("SmoothStrRate") && cs_val["SmoothStrRate"].is_boolean()) {
                prop.smooth_strain_rate = cs_val["SmoothStrRate"].get<bool>();
            }
            fill_array_double("RelaVecCoeff", prop.rela_vec_coeff);
            fill_array_double("RelaVecExp", prop.rela_vec_exp);
            fill_array_double("FailureScale", prop.failure_scale);
            fill_array_double("FailureExp", prop.failure_exp);

            registry.emplace<Component::BeamSpringProperty>(cs_entity, std::move(prop));
        }
        // Other types: just keep SetName for future extension
    }
}

// ---------------------------------------------------------------
// Sub-parser: Material definitions
// ---------------------------------------------------------------

void SimdroidParser::parse_materials(
    const nlohmann::json& j, entt::registry& registry,
    DofMap* dof_map, const std::filesystem::path& control_dir)
{
    if (!j.contains("Material") || !j["Material"].is_object()) return;

    for (auto& [key, val] : j["Material"].items()) {
        const entt::entity mat_e = registry.create();

        // Material ID (MID) - primary key connecting elements and materials
        if (val.contains("MID") && val["MID"].is_number_integer()) {
            registry.emplace<Component::MaterialID>(mat_e, val["MID"].get<int>());
        }

        // Material type (prefer Simdroid's "MaterialType", fallback to legacy "Type")
        const std::string mat_type = val.value("MaterialType", val.value("Type", ""));
        if (!mat_type.empty()) {
            registry.emplace<Component::MaterialModel>(mat_e, mat_type);
        }

        // Common: density + linear elastic constants
        Component::LinearElasticParams params{};
        if (val.contains("Density")) params.rho = val["Density"].get<double>();

        if (val.contains("MaterialConstants") && val["MaterialConstants"].is_object()) {
            const auto& cons = val["MaterialConstants"];

            // Support both canonical keys and older variants.
            if (cons.contains("ElasticModulus")) params.E = cons["ElasticModulus"].get<double>();
            else if (cons.contains("E")) params.E = cons["E"].get<double>();
            else if (cons.contains("YoungModulus")) params.E = cons["YoungModulus"].get<double>();

            if (cons.contains("PoissonRatio")) params.nu = cons["PoissonRatio"].get<double>();
            else if (cons.contains("Nu")) params.nu = cons["Nu"].get<double>();
            else if (cons.contains("nu")) params.nu = cons["nu"].get<double>();

            registry.emplace<Component::LinearElasticParams>(mat_e, params);

            // LAW2: IsotropicPlasticJC (Johnson-Cook)
            if (mat_type == "IsotropicPlasticJC") {
                Component::IsotropicPlasticParams pl{};
                if (cons.contains("YieldStress")) pl.yield_stress_A = cons["YieldStress"].get<double>();
                if (cons.contains("HardeningCoefB")) pl.hardening_coef_B = cons["HardeningCoefB"].get<double>();
                if (cons.contains("HardeningExpN")) pl.hardening_exp_n = cons["HardeningExpN"].get<double>();
                if (cons.contains("RateCoef")) pl.rate_coef_C = cons["RateCoef"].get<double>();
                if (cons.contains("HardeningMode")) pl.hardening_mode = cons["HardeningMode"].get<double>();
                if (cons.contains("TemperatureExp")) pl.temperature_exp_m = cons["TemperatureExp"].get<double>();
                if (cons.contains("MeltTemperature")) pl.melt_temperature = cons["MeltTemperature"].get<double>();
                if (cons.contains("EnvTemperature")) pl.env_temperature = cons["EnvTemperature"].get<double>();
                if (cons.contains("RefStrainRate")) pl.ref_strain_rate = cons["RefStrainRate"].get<double>();
                if (cons.contains("SpecificHeat")) pl.specific_heat = cons["SpecificHeat"].get<double>();
                registry.emplace<Component::IsotropicPlasticParams>(mat_e, std::move(pl));
            }

            // LAW36: RateDependentPlastic
            if (mat_type == "RateDependentPlastic") {
                Component::RateDependentPlasticParams rdp{};
                if (cons.contains("HardeningMode")) rdp.hardening_mode = cons["HardeningMode"].get<double>();
                if (cons.contains("FailurePlasticStrain")) rdp.failure_plastic_strain = cons["FailurePlasticStrain"].get<double>();
                if (cons.contains("FailBeginTensileStrain")) rdp.fail_begin_tensile_strain = cons["FailBeginTensileStrain"].get<double>();
                if (cons.contains("FailEndTensileStrain")) rdp.fail_end_tensile_strain = cons["FailEndTensileStrain"].get<double>();
                if (cons.contains("ElemDelTensileStrain")) rdp.elem_del_tensile_strain = cons["ElemDelTensileStrain"].get<double>();
                if (cons.contains("StrainRateType")) rdp.strain_rate_type = cons["StrainRateType"].get<std::string>();
                if (cons.contains("StrainAndStrainRateYieldCurve") && cons["StrainAndStrainRateYieldCurve"].is_array()) {
                    rdp.yield_curves = cons["StrainAndStrainRateYieldCurve"].get<std::vector<std::string>>();
                }
                if (cons.contains("StrainRate") && cons["StrainRate"].is_array()) {
                    rdp.strain_rates = cons["StrainRate"].get<std::vector<double>>();
                }
                registry.emplace<Component::RateDependentPlasticParams>(mat_e, std::move(rdp));
            }

            // Hyperelastic materials (Polynomial / ReducedPolynomial / Ogden*)
            auto map_curve_names_to_entities = [&](const char* json_key, std::vector<entt::entity>& out_entities) {
                if (!cons.contains(json_key) || !cons[json_key].is_array()) return;
                for (const auto& name_val : cons[json_key]) {
                    if (!name_val.is_string()) continue;
                    const std::string name = name_val.get<std::string>();
                    entt::entity curve_e = get_or_create_curve_entity(name, registry, dof_map, control_dir);
                    out_entities.push_back(curve_e);
                }
            };

            Component::HyperelasticMode hyper{};
            bool has_hyperelastic = false;

            // --- Polynomial (full) ---
            if (mat_type == "Polynomial") {
                if (cons.contains("Order") && cons.contains("Const") && cons["Const"].is_array()) {
                    const int order = cons["Order"].get<int>();
                    std::vector<double> all;
                    all.reserve(cons["Const"].size());
                    for (const auto& v : cons["Const"]) {
                        if (v.is_number()) all.push_back(v.get<double>());
                    }

                    if (order > 0 && static_cast<int>(all.size()) >= order) {
                        const int total = static_cast<int>(all.size());
                        const int d_count = order;
                        const int c_count = total - d_count;

                        Component::PolynomialParams poly{};
                        poly.c_ij.assign(all.begin(), all.begin() + c_count);
                        poly.d_i.assign(all.begin() + c_count, all.end());

                        registry.emplace<Component::PolynomialParams>(mat_e, std::move(poly));

                        hyper.order = order;
                        hyper.fit_from_data = false;
                        hyper.nu = params.nu;
                        has_hyperelastic = true;
                    }
                }
            }

            // --- ReducedPolynomial ---
            if (mat_type == "ReducedPolynomial") {
                if (cons.contains("Order") && cons.contains("Const") && cons["Const"].is_array()) {
                    const int order = cons["Order"].get<int>();
                    std::vector<double> all;
                    all.reserve(cons["Const"].size());
                    for (const auto& v : cons["Const"]) {
                        if (v.is_number()) all.push_back(v.get<double>());
                    }

                    if (order > 0 && static_cast<int>(all.size()) == 2 * order) {
                        Component::ReducedPolynomialParams rpoly{};
                        rpoly.c_i0.assign(all.begin(), all.begin() + order);
                        rpoly.d_i.assign(all.begin() + order, all.end());

                        registry.emplace<Component::ReducedPolynomialParams>(mat_e, std::move(rpoly));

                        hyper.order = order;
                        hyper.fit_from_data = false;
                        hyper.nu = params.nu;
                        has_hyperelastic = true;
                    }
                }
            }

            // --- Ogden family (OgdenRubber / Ogden2 / Ogden) ---
            if (mat_type == "OgdenRubber" || mat_type == "Ogden2" || mat_type == "Ogden") {
                Component::OgdenParams og{};

                if (cons.contains("Mu") && cons["Mu"].is_array()) {
                    for (const auto& v : cons["Mu"]) {
                        if (v.is_number()) og.mu_i.push_back(v.get<double>());
                    }
                }
                if (cons.contains("Alpha") && cons["Alpha"].is_array()) {
                    for (const auto& v : cons["Alpha"]) {
                        if (v.is_number()) og.alpha_i.push_back(v.get<double>());
                    }
                }
                if (cons.contains("D") && cons["D"].is_array()) {
                    for (const auto& v : cons["D"]) {
                        if (v.is_number()) og.d_i.push_back(v.get<double>());
                    }
                }

                if (!og.mu_i.empty() && og.mu_i.size() == og.alpha_i.size()) {
                    registry.emplace<Component::OgdenParams>(mat_e, std::move(og));

                    hyper.order = static_cast<int>(og.mu_i.size());
                    hyper.fit_from_data = false;
                    hyper.nu = params.nu;
                    has_hyperelastic = true;
                } else if (!og.mu_i.empty() || !og.alpha_i.empty()) {
                    spdlog::warn("Ogdeng material '{}' has inconsistent Mu/Alpha lengths. Ignoring direct parameters.", key);
                }
            }

            // --- TestCurve mode (experimental curves) ---
            bool has_curve = false;
            if (cons.contains("TestCurve-Uniaxial") || cons.contains("TestCurve-Biaxial") ||
                cons.contains("TestCurve-Planar")   || cons.contains("TestCurve-Volumetric")) {
                has_curve = true;
            }

            if (has_curve) {
                hyper.fit_from_data = true;

                if (cons.contains("CurveFit_n") && cons["CurveFit_n"].is_number_integer()) {
                    hyper.order = cons["CurveFit_n"].get<int>();
                } else if (hyper.order <= 0) {
                    hyper.order = 1;
                }

                if (cons.contains("CurveFit_Nu") && cons["CurveFit_Nu"].is_number()) {
                    hyper.nu = cons["CurveFit_Nu"].get<double>();
                } else if (cons.contains("PoissonRatio") && cons["PoissonRatio"].is_number()) {
                    hyper.nu = cons["PoissonRatio"].get<double>();
                } else {
                    hyper.nu = params.nu;
                }

                map_curve_names_to_entities("TestCurve-Uniaxial",   hyper.uniaxial_funcs);
                map_curve_names_to_entities("TestCurve-Biaxial",    hyper.biaxial_funcs);
                map_curve_names_to_entities("TestCurve-Planar",     hyper.planar_funcs);
                map_curve_names_to_entities("TestCurve-Volumetric", hyper.volumetric_funcs);

                has_hyperelastic = true;
            }

            if (has_hyperelastic) {
                registry.emplace<Component::HyperelasticMode>(mat_e, std::move(hyper));
            }
        } else {
            // Still store density-only materials to keep linkage stable.
            if (val.contains("Density")) {
                registry.emplace<Component::LinearElasticParams>(mat_e, params);
            }
        }
        registry.emplace<Component::SetName>(mat_e, key);
    }
}

// ---------------------------------------------------------------
// Sub-parser: PartProperty (links ElementSet + Material + CrossSection)
// ---------------------------------------------------------------

void SimdroidParser::parse_part_properties(
    const nlohmann::json& j, entt::registry& registry,
    const std::unordered_map<std::string, entt::entity>& cross_section_map)
{
    if (!j.contains("PartProperty") || !j["PartProperty"].is_object()) return;

    for (auto it = j["PartProperty"].begin(); it != j["PartProperty"].end(); ++it) {
        const std::string part_key = it.key();
        const auto& part_info = it.value();

        std::string title = part_info.value("Title", part_key);
        std::string ele_set_name = part_info.value("EleSet", "");
        std::string mat_name = part_info.value("Material", "");
        std::string cs_name  = part_info.value("CrossSection", "");

        entt::entity ele_set_entity = entt::null;
        if (!ele_set_name.empty()) {
            ele_set_entity = get_or_create_set_entity(registry, ele_set_name);
            registry.get_or_emplace<Component::ElementSetMembers>(ele_set_entity);
        }

        entt::entity mat_entity = entt::null;
        if (!mat_name.empty()) {
            auto view = registry.view<Component::SetName, Component::LinearElasticParams>();
            for(auto e : view) {
                if(view.get<Component::SetName>(e).value == mat_name) {
                    mat_entity = e;
                    break;
                }
            }
        }

        entt::entity section_entity = entt::null;
        if (!cs_name.empty()) {
            auto cs_it = cross_section_map.find(cs_name);
            if (cs_it != cross_section_map.end()) {
                section_entity = cs_it->second;
            } else {
                // Degraded case: CrossSection block missing but PartProperty references name
                section_entity = registry.create();
                registry.emplace<Component::SetName>(section_entity, cs_name);
            }
        }

        const entt::entity part_entity = registry.create();
        Component::SimdroidPart part;
        part.name = std::move(title);
        part.element_set = ele_set_entity;
        part.material = mat_entity;
        part.section = section_entity;
        registry.emplace<Component::SimdroidPart>(part_entity, std::move(part));
    }
}

// ---------------------------------------------------------------
// Sub-parser: Contact definitions
// ---------------------------------------------------------------

void SimdroidParser::parse_contacts(
    const nlohmann::json& j, entt::registry& registry, DofMap* dof_map)
{
    if (!j.contains("Contact") || !j["Contact"].is_object()) return;

    for (auto& [contact_name, ci] : j["Contact"].items()) {
        const entt::entity e = registry.create();

        // --- ContactTypeTag ---
        const std::string type_str = ci.value("Type", "");
        const auto inter_type = lookup_enum(kContactTypeMap, type_str,
                                            Component::ContactInterType::Unknown);
        registry.emplace<Component::ContactTypeTag>(e, inter_type);

        // --- ContactBase (master / slave entity handles) ---
        Component::ContactBase base;
        base.name = contact_name;
        std::string master_name, slave_name;
        if (inter_type == Component::ContactInterType::General) {
            master_name = ci.value("Surf1", "");
            slave_name  = ci.value("Surf2", "");
        } else {
            master_name = ci.value("MasterFaces", "");
            slave_name  = ci.value("SlaveNodes", ci.value("SlaveFaces", ""));
        }
        if (!master_name.empty()) base.master_entity = get_or_create_set_entity(registry, master_name);
        if (!slave_name.empty())  base.slave_entity  = get_or_create_set_entity(registry, slave_name);
        registry.emplace<Component::ContactBase>(e, std::move(base));
        registry.emplace<Component::SetName>(e, contact_name);

        // --- ContactFormulation ---
        {
            Component::ContactFormulation form{};
            bool has_form = false;
            if (ci.contains("Formulation")) {
                form.formulation = lookup_enum(kFormulationMap,
                    ci["Formulation"].get<std::string>(),
                    Component::ContactFormulationType::Standard);
                has_form = true;
            }
            if (ci.contains("StiffnessFactor")) {
                form.stiffness_factor = ci["StiffnessFactor"].get<double>();
                has_form = true;
            }
            if (ci.contains("DampingCoefficient")) {
                form.damping_coefficient = ci["DampingCoefficient"].get<double>();
                has_form = true;
            }
            if (ci.contains("Damping")) {
                form.damping_coefficient = ci["Damping"].get<double>();
                has_form = true;
            }
            if (ci.contains("InterfaceStiffness")) {
                form.interface_stiffness = lookup_enum(kInterfaceStiffnessMap,
                    ci["InterfaceStiffness"].get<std::string>(),
                    Component::InterfaceStiffnessType::Default);
                has_form = true;
            }
            if (ci.contains("StiffnessForm")) {
                if (ci["StiffnessForm"].is_number_integer()) {
                    form.stiffness_form = ci["StiffnessForm"].get<int>();
                } else if (ci["StiffnessForm"].is_string()) {
                    auto it2 = kStiffnessFormMap.find(to_lower_copy(ci["StiffnessForm"].get<std::string>()));
                    if (it2 != kStiffnessFormMap.end()) form.stiffness_form = it2->second;
                }
                has_form = true;
            }
            if (ci.contains("MinStiffness")) { form.min_stiffness = ci["MinStiffness"].get<double>(); has_form = true; }
            if (ci.contains("MaxStiffness")) { form.max_stiffness = ci["MaxStiffness"].get<double>(); has_form = true; }
            if (ci.contains("SearchMethod")) {
                form.search_method = lookup_enum(kSearchMethodMap,
                    ci["SearchMethod"].get<std::string>(),
                    Component::SearchMethodType::Segment);
                has_form = true;
            }
            if (ci.contains("SearchDistance")) { form.search_distance = ci["SearchDistance"].get<double>(); has_form = true; }
            if (has_form) registry.emplace<Component::ContactFormulation>(e, form);
        }

        // --- ContactFriction ---
        {
            Component::ContactFriction fric{};
            bool has_fric = false;
            if (ci.contains("FrictionLaw")) {
                fric.friction_law = lookup_enum(kFrictionLawMap,
                    ci["FrictionLaw"].get<std::string>(),
                    Component::FrictionLawType::Coulomb);
                has_fric = true;
            }
            if (ci.contains("FrictionCoef")) { fric.friction_coef = ci["FrictionCoef"].get<double>(); has_fric = true; }
            if (ci.contains("Friction"))     { fric.friction_coef = ci["Friction"].get<double>();     has_fric = true; }
            if (ci.contains("FrictionCoefs") && ci["FrictionCoefs"].is_array()) {
                const auto& arr = ci["FrictionCoefs"];
                for (size_t k = 0; k < std::min(arr.size(), size_t(6)); ++k)
                    fric.friction_coefs[k] = arr[k].get<double>();
                has_fric = true;
            }
            if (ci.contains("DampingInterStiff")) { fric.damping_inter_stiff = ci["DampingInterStiff"].get<double>(); has_fric = true; }
            if (ci.contains("DampingInterFric"))  { fric.damping_inter_fric  = ci["DampingInterFric"].get<double>();  has_fric = true; }
            if (has_fric) registry.emplace<Component::ContactFriction>(e, fric);
        }

        // --- ContactGapControl ---
        {
            Component::ContactGapControl gap{};
            bool has_gap = false;
            if (ci.contains("GapFlag")) {
                gap.gap_flag = lookup_enum(kGapFlagMap,
                    ci["GapFlag"].get<std::string>(),
                    Component::GapFlagType::None);
                has_gap = true;
            }
            if (ci.contains("GapScale"))      { gap.gap_scale       = ci["GapScale"].get<double>();      has_gap = true; }
            if (ci.contains("GapMin"))         { gap.gap_min         = ci["GapMin"].get<double>();         has_gap = true; }
            if (ci.contains("GapMax"))         { gap.gap_max         = ci["GapMax"].get<double>();         has_gap = true; }
            if (ci.contains("SlaveGapMax"))    { gap.slave_gap_max   = ci["SlaveGapMax"].get<double>();    has_gap = true; }
            if (ci.contains("MasterGapMax"))   { gap.master_gap_max  = ci["MasterGapMax"].get<double>();   has_gap = true; }
            if (has_gap) registry.emplace<Component::ContactGapControl>(e, gap);
        }

        // --- ContactTieData (TYPE2-specific: Ignore, fail mode, curve refs) ---
        if (inter_type == Component::ContactInterType::Tie) {
            Component::ContactTieData tie{};
            if (ci.contains("Ignore")) {
                tie.ignore = lookup_enum(kIgnoreMap,
                    ci["Ignore"].get<std::string>(),
                    Component::IgnoreType::MainThick);
            }
            if (ci.contains("FailMode")) {
                tie.fail_mode = lookup_enum(kFailModeMap,
                    ci["FailMode"].get<std::string>(),
                    Component::FailModeType::None);
            }
            if (ci.contains("RuptureFlag")) {
                tie.rupture_flag = lookup_enum(kRuptureFlagMap,
                    ci["RuptureFlag"].get<std::string>(),
                    Component::RuptureFlagType::TractionCompress);
            }
            if (ci.contains("Max_N_Dist")) tie.max_n_dist = ci["Max_N_Dist"].get<double>();
            if (ci.contains("Max_T_Dist")) tie.max_t_dist = ci["Max_T_Dist"].get<double>();

            // Curve references (via DofMap)
            auto resolve_curve = [&](const char* key) -> entt::entity {
                if (!ci.contains(key) || !ci[key].is_string()) return entt::null;
                std::string cname = ci[key].get<std::string>();
                trim(cname);
                if (cname.empty() || !dof_map) return entt::null;
                auto cit = dof_map->curve_name_to_entity.find(cname);
                if (cit != dof_map->curve_name_to_entity.end()) return cit->second;
                return get_or_create_curve_entity(cname, registry, dof_map, std::filesystem::path());
            };
            tie.stress_vs_stress_rate = resolve_curve("StressVsStressRate");
            tie.nor_stress_vs_disp    = resolve_curve("NorStressVsDisp");
            tie.tang_stress_vs_disp   = resolve_curve("TangStressVsDisp");

            registry.emplace<Component::ContactTieData>(e, tie);
        }

        spdlog::info("  -> Contact '{}' (Type={}) created.", contact_name, type_str);
    }
}

// ---------------------------------------------------------------
// Main dispatcher: parse_control_json()
// ---------------------------------------------------------------

void SimdroidParser::parse_control_json(const std::string& path, DataContext& ctx) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("cannot open control file");
    json j = json::parse(file, nullptr, true, true);

    // Save raw JSON as blueprint in DataContext
    ctx.simdroid_blueprint = j;
    spdlog::info("Simdroid blueprint saved. Unknown fields will be preserved during export.");

    auto& registry = ctx.registry;

    // -----------------------------------------------------------------
    // Setup DofMap for Curve entity management
    // -----------------------------------------------------------------
    DofMap* dof_map = nullptr;
    if (registry.ctx().contains<DofMap>()) {
        dof_map = &registry.ctx().get<DofMap>();
    } else {
        dof_map = &registry.ctx().emplace<DofMap>();
    }

    // Control file directory for locating same-name .txt files
    std::filesystem::path control_dir = std::filesystem::path(path).parent_path();

    // -----------------------------------------------------------------
    // Pre-parse: Function names -> Curve entities
    // -----------------------------------------------------------------
    if (j.contains("Function") && j["Function"].is_object()) {
        for (auto& [fname, fval] : j["Function"].items()) {
            (void)fval;
            (void)get_or_create_curve_entity(fname, registry, dof_map, control_dir);
        }
        spdlog::info("Simdroid Functions parsed: {} entries mapped to Curve entities.", dof_map->curve_name_to_entity.size());
    }

    // -----------------------------------------------------------------
    // CrossSection: section/property definitions -> Property entities
    // -----------------------------------------------------------------
    std::unordered_map<std::string, entt::entity> cross_section_map;
    parse_cross_sections(j, registry, cross_section_map, dof_map);

    // -----------------------------------------------------------------
    // Material: material model definitions
    // -----------------------------------------------------------------
    parse_materials(j, registry, dof_map, control_dir);

    // -----------------------------------------------------------------
    // PartProperty: links ElementSet + Material + CrossSection
    // -----------------------------------------------------------------
    parse_part_properties(j, registry, cross_section_map);

    // -----------------------------------------------------------------
    // Contact: contact definitions
    // -----------------------------------------------------------------
    parse_contacts(j, registry, dof_map);

    // -----------------------------------------------------------------
    // Constraint dispatch
    // -----------------------------------------------------------------
    if (j.contains("Constraint") && j["Constraint"].is_object()) {
        spdlog::info("Parsing Constraints from Simdroid Control...");
        const auto& j_cons = j["Constraint"];

        if (j_cons.contains("Boundary")) {
            parse_boundary_conditions(j_cons["Boundary"], registry);
        }
        if (j_cons.contains("NodalRigidBody")) {
            parse_rigid_bodies(j_cons["NodalRigidBody"], registry);
        }
        if (j_cons.contains("DistributingCoupling")) {
            parse_rigid_bodies(j_cons["DistributingCoupling"], registry);
        }
        if (j_cons.contains("RigidWall")) {
            spdlog::info("Parsing RigidWalls...");
            parse_rigid_walls(j_cons["RigidWall"], registry);
        }
    }

    // Radioss /RBODY rigid bodies (top-level)
    if (j.contains("RigidBody") && j["RigidBody"].is_object()) {
        spdlog::info("Parsing Radioss RigidBodies...");
        parse_radioss_rigid_bodies(j["RigidBody"], registry);
    }

    // Load
    if (j.contains("Load") && j["Load"].is_object()) {
        spdlog::info("Parsing Loads from Simdroid Control...");
        parse_loads(j["Load"], registry);
    }

    // Initial Conditions
    if (j.contains("InitialCondition") && j["InitialCondition"].is_object()) {
        spdlog::info("Parsing Initial Conditions...");
        parse_initial_conditions(j["InitialCondition"], registry);
    }

    // Analysis Settings (Step)
    if (j.contains("Step") && j["Step"].is_object()) {
        spdlog::info("Parsing Analysis Settings...");
        parse_analysis_settings(j["Step"], registry, ctx);
    }

    // Post-parse: validate entity references & cross-constraint overlap
    validate_contacts(registry);
    validate_rigid_bodies(registry);
    validate_cross_constraints(registry);
}
