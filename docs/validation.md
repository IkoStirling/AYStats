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

## Gameplay facade follow-up — 2026-09-30

Implemented and verified named RecipeBook selection/input provenance, owner-side
increase/decrease/set, shared single/batch attribute translation, owned prepared
plans, atomic entity changeBatch and independent weak StateObserver publication.
The module remains experimental. These follow-up changes are retained in the
working tree; the last local baseline commit is `2d73aec`, with no remote. Shared
pre-existing/parallel work was preserved rather than staged into a mixed commit.

| Configuration | Passed cases | Evidence |
| --- | ---: | --- |
| CPU-only headless, full with inventory audit | 62, 4 partitions | [case bundle](gameplay-validation/cae64f3cd80b4814bb45d8fe56cfc7fe/cases.json) |
| Tools/editor, full with inventory audit | 65, 7 partitions | [case bundle](gameplay-editor-validation/c35d6fec2bc942ea851f90cd9405aeba/cases.json) |
| Standalone hardware GPU | 3, no skips | [case bundle](gameplay-gpu-validation/290211a2ccc34c70aecb40e895b8c9da/cases.json) |
| AYCombat consumer, full with inventory audit | 70, 3 partitions | [case bundle](gameplay-combat-validation/a97a71692af648849d2d1f46516dcf2e/cases.json) |

The 65 CPU cases comprise 40 Core/Batch/Assets cases, one stress case,
21 Entity/Scene/Script/Network/gameplay cases, and one each for Host, Inspector and
Editor. Thus 68 distinct module cases pass including GPU; separate headless and
editor runs are not counted twice. Full AYEditor Release library and optional
Stats adapters were rebuilt. SDK doctor now checks 63 capabilities, 50 modules,
12 search cases and 82 entry symbols, with zero errors/warnings.

The new gameplay evidence covers direct pool/current and scalar/base behavior,
explicit/default/missing/incompatible recipe selection, source provenance, plan
lifetime after World teardown, sequential/batch parity, Snapshot/Ordered distinction,
actual 80/20 attribution, atomic failure, duplicate IDs within one submission,
stale replay/generation rejection, whole-bank callback ordering, replica authority,
preflight exception/rejection, weak observer capacity/lifetime, Configure/Restore/
Replica/Detach and effect update/removal/expiry. A declared shared read-only source
feeds 130 independent targets through the real four-worker scheduler and matches
the scalar reference, while invalid fields/overflow leave the bank unchanged.

The runnable `AYStats_GameplayExample` loads authored definition/plan JSON. Its
observed output is attack 198, shield absorption 30, requested/actual/excess health
damage 69/50/19, actual leech 10, source/target health 50/0, restored health 50/0,
attack after expiry 100, one depletion notification and rejected late publication.
Its same scenario runs in the Entity suite. Example policy stays outside Core;
AYCombat regressions verify the separate permission/life integration.

Reproduce with the existing configured profiles and toolchain:

```powershell
cmake --build out/build/stats-engine --target AYStats_UnitTests AYStats_StressTests AYStats_EntityTests AYStats_GameplayExample
pwsh -NoProfile -File scripts/tests/run-module-tests.ps1 -Module AYStats -Tier full -BuildDirectory out/build/stats-engine -SkipBuild -Audit
pwsh -NoProfile -File scripts/tests/run-module-tests.ps1 -Module AYStats -Tier full -BuildDirectory out/build/windows-tools-release -SkipBuild -Audit
pwsh -NoProfile -File scripts/tests/run-module-tests.ps1 -Module AYStats -Tier gpu -BuildDirectory out/build/stats-core -SkipBuild
pwsh -NoProfile -File scripts/tests/run-module-tests.ps1 -Module AYCombat -Tier full -BuildDirectory out/build/combat-engine-verified -SkipBuild -Audit
pwsh -NoProfile -File tools/ayapi/ayapi.ps1 doctor
```

[Source fingerprint](gameplay-validation-sources.json) records the observed working
tree source content for this validation, not a release artifact. Initial invalid
negative-persistent-value test fixtures were corrected to the existing non-negative
persistent profile; intermediate arithmetic remains signed. No new performance
claim is made for the gameplay facade. RecipeBook asset loading/editor selection,
CPU settlement allocation/copy optimization, advanced resource packaging and the
previously listed production/device-fault verification remain future work.

## Complete CPU settlement acceptance — 2026-09-30

The previous follow-up's copy/allocation optimization item is now complete.
Each bank state is copied once, unpublished shadows commit in place, task input/
plan/rebuild buffers are reused, dependency grouping uses written objects and a
stable scatter, and identity validation uses a flat sorted array. Profiling is
optional; Entity profiling includes real preflight, internal propagation/state
transport and public callbacks. Accepted tasks drain on submission refusal or
exception; the scheduler must reject/throw before acceptance and wait normally.

Acceptance reports:

- [CPU-only headless full](settlement-validation/84ab0d81536540a5a718d787ff092525/cases.json): 67 cases, four partitions, all pass.
- [Host/Inspector/Editor full](settlement-editor-validation/1042651c9afa4b3282cfc0bda5351c84/cases.json): 70 cases, seven partitions, all pass; includes the headless cases.
- [Real hardware GPU](settlement-gpu-validation/22acad46b1194d538eefe8b292a45ba3/cases.json): three cases, all pass, zero skips.
- [AYCombat consumer](settlement-combat-validation/1e7542d9ceb74d4fba5bf61defbe8033/cases.json): 70 cases, three partitions, all pass.

Thus 73 distinct AYStats cases pass including GPU, plus 70 consumer cases.
The full AYEditor Release library was relinked after rebuilding affected adapters.
New randomized evidence compares each result version/change field and final row
against public atomic local transactions over 700 mixed requests / 37 states /
two read semantics / three capacity policies. It includes modifiers, derived
base and capacity, direct capacity changes and multi-target writes. Additional
tests cover later-writer dependencies/read-only requests, late derived overflow
discarding all shadows, accepted-work draining on scheduler failure, and real
Entity profiling with full-bank callback/observer ordering and stale retry reset.

[Performance report](settlement-performance.md) separates uninstrumented latency,
instrumented allocation/stages, caller construction, result destruction and real
Entity notification work. All 18 combinations at 1k/100k/1M match before/after
checksums, with five recorded latency samples per combination. Million-request
core median is 193–367 ms, improving by 2.12–3.16× against the same workload;
requested C++ allocation calls fall about 67%, incremental peak payload about
29%. The preserved baseline source and reconstruction script reproduce all 18
checksums; allocation counts agree within one scheduler allocation/eight bytes.
No GPU authority or automatic global GPU
threshold is inferred from CPU settlement measurements.

SDK doctor checks 63 capabilities, 50 modules, 12 search cases and 85 documented
entry symbols with zero errors/warnings. See [current source fingerprint](settlement-validation-sources.json).
All changes remain in the existing working trees; no remote was added and no
push/publish was performed. RecipeBook resource loading/editor selection,
formal cook/pak and earlier production/device-fault validation remain future work.
