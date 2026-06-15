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

The first implementation stage is a planning and validation layer:

- `ConnectionPreservingRemesher::build_plan(...)`
  builds a machine-readable reduction plan from the current ECS registry and `SimdroidInspector`.
- `ConnectionPreservingRemesher::validate_preservation(before, after)`
  checks whether Part signatures and Interface signatures were preserved.
- `remesh_plan [output.json] [ratio]`
  interactive command that writes the plan as JSON.

The current stage does not yet replace the physical mesh. It creates the invariant contract that later mesh coarsening and regeneration must satisfy.

## CLI Usage

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

## Planned Mesh Generation Stages

1. Extract protected boundary/interface entities for each Part.
2. Build coarse Part-local replacement meshes while preserving each Part element type.
3. Reconstruct interface sets for Contact, Tie, MPC, and SharedNode topology.
4. Reapply loads and constraints through decoupled boundary sets.
5. Build a before/after remesh plan and run preservation validation.

The implementation should reject output if validation fails.
