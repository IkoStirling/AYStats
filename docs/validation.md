# First-version validation — 2026-09-29

## Local source and ownership

AYStats is an independent repository on `main` with no remote. Root integration is opt-in (`AY_ENABLE_STATS`, default OFF). Root SDK/CMake/test runner and AYEditor integration changes remain in their own existing working trees; they are not folded into this module's repository. Other pre-existing changes were retained.

## Successful configurations

- Standalone Release Core/Batch/Assets/GPU: `out/build/stats-core`.
- CPU-only headless root: `out/build/stats-engine`; Script and Network enabled, Application/Editor/render/device/audio/physics/2D/voxel/video/particle disabled. Root Entity/Scene integration tests pass without a GPU dependency.
- Standard tools/editor root: `out/build/windows-tools-release`; Stats, Application, AYUI, Scene, Script and Network enabled. Complete AYEditor Release static library built. Particle was disabled for this isolated Stats validation after its unrelated shader bake failed; no Particle implementation was changed.
- Independent pure Core: system TEMP `aystats-pure-core-20260929`, BUILD_TESTING/BATCH/ASSETS/GPU OFF; only C++23 standard-library sources compiled into AYStatsCore.
- Existing Combat consumer: `out/build/combat-engine-verified`, rebuilt against the current Stats Core/Batch/Assets/Entity; 52 Combat cases pass with inventory audit.

Build products partly use system TEMP because the workspace volume had insufficient space. One generated Editor object directory is redirected to TEMP by a junction. Source, public APIs and runtime asset paths do not depend on these machine-specific locations; ordinary CMake output paths remain configurable. Fresh build helpers/toolchains can regenerate the discarded headless object files.

## Functional checks

| Target / tier | Passed cases | Important boundary |
| --- | ---: | --- |
| AYStats_UnitTests fast + integration | 31 | int64 boundaries/widened product, atomic actual attribution, zones/identities, dependency cycles, capacity policies, exact random/trace, effect expiry/refresh/stack, budget/cancellation/CPU equality, strict JSON/checkpoints and pure authoring |
| AYStats_StressTests | 1 | 32 heterogeneous recipes × 2,051 lanes; 1/4 workers; 31/4,096 block sizes; exact reference equality |
| AYStats_EntityTests | 9 | zero initial state, Scene save/reload, authority/schema/handle/generation checks, callbacks/deferred commands, old Health conversion, real Logia and network serializer/replication adapter, Scene events |
| AYStats_HostTests | 1 | actual EngineModuleRuntime prepare/seal/install/shutdown and Scene subscription cleanup |
| AYStats_InspectorTests | 1 | real widget construction, exact zero author current, independent live state and read-only view |
| AYStats_EditorTests | 1 | real EditorSceneDocument author edit/undo/redo/save/reopen; runtime quantity does not overwrite author initial zero |
| AYStats_GpuTests (hardware) | 3 | FL11 hardware, signed integer parity, 1/257/10,003 lane tails, strict32 bounds/rejection, Automatic CPU fallback, cancellation/device lifetime and invalid backend choice |
| DefaultEditorModuleAssemblyTest | 1 | updated optional Stats module ordering and complete startup/shutdown |

CPU-only full tier audits **44 cases in 7 partitions**, without omissions/duplicates; GPU is an independent device tier with **3 cases passed and no skips**. Thus 47 module cases pass, plus the editor assembly regression. [CPU case bundle](validation-cases/b4edbf8bc617431eb1152c7b190f7297/cases.json) and [GPU case bundle](gpu-validation-cases/ab5a65a62fd349399886ce27dc8351b1/cases.json) retain source case locations, assertions, process results and status.

Agent SDK doctor checks 63 capabilities, 50 modules, 12 retrieval search cases and 75 documented entry symbols: **0 errors, 0 warnings**. Six Stats capabilities have current headers, contracts and executable evidence; coverage is `covered`, capability stability is `experimental`.

## Performance and remaining verification

Release measurements cover 1/1k/10k/100k/1M pure CPU, simple and 48-stage CPU/GPU comparison, and hotspot/distributed/cross-dependent Snapshot/Ordered bank settlement. The isolated C++ allocation probe records accepted task submissions and peak incremental outstanding C++ bytes. See [performance.md](performance.md) for raw files, exact measurement boundaries, drivers, counter scope and Automatic policy.

This does not claim a complete interactive UI smoke, GPU device-loss fault injection, live socket transport, every cache-thrashing workload, GPU timestamp separation or device-side shared-state settlement. The network test uses the actual existing serializer/adapter rather than a socket. Editor document history and persistence are automated; full user interaction remains product-level verification. CPU/GPU tests and end-to-end timings are actual evidence, while explicitly unsupported future features remain listed in README/design/SDK.
