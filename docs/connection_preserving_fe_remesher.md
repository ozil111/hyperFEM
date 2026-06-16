# Connection-Preserving FE Remesher

Connection-Preserving FE Remesher is a debug-oriented model reduction workflow. Its goal is to produce a much smaller finite element model while preserving solver-relevant connectivity contracts.

The output is for bug reproduction, solver debugging, regression tests, and development verification only. It is not valid for engineering analysis, design decisions, displacement accuracy, stress accuracy, or frequency accuracy.

## Preserved Contracts

The remesher treats inter-part connectivity as the primary invariant:

- Part identity is preserved.
- Element type set per Part is preserved.
- Property type and material type per Part are preserved.
- Contact, Tie, MPC, and SharedNode interface identities are preserved.
- Contact and constraint formulation metadata should remain unchanged when present in the ECS model.

Changing any of these contracts is considered a remeshing failure, even if the reduced mesh solves successfully.

## Current Implementation

The implementation currently has two layers:

1. A planning and validation layer.
2. A limited structured Hex8 mesh-generation layer for a single-Part debug case.

### Planning and Validation

- `ConnectionPreservingRemesher::build_plan(...)`
  builds a machine-readable reduction plan from the current ECS registry and `SimdroidInspector`.
- `ConnectionPreservingRemesher::validate_preservation(before, after)`
  checks whether Part signatures and Interface signatures were preserved.
- `ConnectionPreservingRemesher::write_plan_json(...)`
  writes a plan to JSON.
- `remesh_plan [output.json] [ratio]`
  interactive command that writes the plan as JSON.

### Structured Hex8 Generation

- `ConnectionPreservingRemesher::remesh_structured_hex8(...)`
  performs a restricted physical mesh replacement for one structured Hex8 Part.
- `remesh_generate [output_dir] [ratio]`
  interactive command that writes the remeshed Simdroid project and validation artifacts.

This generator is intentionally narrow. It currently supports:

- exactly one `SimdroidPart`;
- exactly one element type, Hex8 (`ElementType=308`);
- node coordinates that form a complete 3D structured grid;
- node-set reconstruction by nearest coarse-grid nodes;
- element-set reconstruction by assigning the new coarse elements;
- surface-set reconstruction from the new exterior coarse faces;
- load and boundary reattachment through the original Simdroid blueprint set names.

It rejects unsupported models instead of silently producing a misleading mesh.

## CLI Usage

Generate a plan only:

```text
import_simdroid path/to/control.json
remesh_plan remesh_plan.json 100
```

The generated JSON contains:

- requested compression options
- original and target element counts
- Part-level target element counts
- element type distributions
- material/property type signatures
- interface signatures

Generate a remeshed debug project:

```text
import_simdroid path/to/control.json
remesh_generate output/remeshed_case 100
```

The output directory contains:

- `mesh.dat`
- `control.json`
- `remesh_before.json`
- `remesh_after.json`
- `remesh_validation.json`
- `remesh_result.json`

`remesh_generate` exports the Simdroid project only after preservation validation succeeds.

## Cantilever Beam Example

The first supported generation case is:

```text
case\cantilever beam\cantilever_beam_inp\control.json
```

Use:

```text
import_simdroid "case\cantilever beam\cantilever_beam_inp\control.json"
remesh_generate "result\cantilever_remesh_100x" 100
```

The source model is a single-Part structured Hex8 cantilever beam:

- `24321` nodes
- `20000` Hex8 elements
- one Part: `Component_1_Set-1`
- material: `IsotropicElastic`
- property type: `SolidAdvancedProperty`
- nodal force set: `load`
- boundary node sets: `NodeValueSet_1` through `NodeValueSet_6`
- exterior surface set: `Model_Outside_Surface`

With `ratio=100`, the current structured generator produces:

- about `459` nodes
- `200` Hex8 elements
- non-empty rebuilt node, element, and surface sets
- preserved Part/material/property/type signatures
- preserved load/constraint Part classification
- empty interface list, because this case has no Contact, Tie, MPC, or SharedNode interface between Parts

Note: in `remesh_after.json`, `summary.original_element_count` is the generated coarse mesh element count. `target_element_count` is the target that would be computed if the already-remeshed model were compressed again with the same ratio.

## Tests

Focused tests are in:

```text
test\test_connection_preserving_remesher.cpp
```

They cover:

- shared-node interface planning on a synthetic two-Part model;
- plan generation for the cantilever beam case;
- structured Hex8 remeshing of the cantilever beam from `20000` to `200` elements.

On Windows MinGW/Clang test builds, the test CMake file now copies runtime DLLs, including GTest DLLs, into the test output directory. After building, the focused test executable can be run directly:

```text
.\bin\Debug\tests\test_connection_preserving_remesher.exe
```

## Planned Mesh Generation Stages

1. Generalize protected boundary/interface extraction for each Part.
2. Add Part-local replacement mesh strategies beyond structured Hex8.
3. Reconstruct interface sets for Contact, Tie, MPC, and SharedNode topology.
4. Reapply loads and constraints through decoupled boundary sets for non-structured meshes.
5. Extend preservation validation to include protected set presence/count policies.
6. Add multi-Part regression cases with Contact/Tie/MPC/SharedNode interfaces.

The implementation should reject output if validation fails.
