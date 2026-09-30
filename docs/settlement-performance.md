# CPU settlement optimization — 2026-09-30

This experiment compares the immediately preceding working-tree implementation
with the optimized implementation using the same standalone Release harness.
It covers 1,000 / 100,000 / 1,000,000 requests, a single-target hotspot,
1,000 independent targets and a cycle of cross-dependent targets, with both
Snapshot and Ordered semantics. Raw samples live in `settlement-performance/`.

Windows x64, AMD Ryzen 7 5800H, MSVC 19.51.36256 / C++23 Release `/O2`,
four AYTask workers, matching the preceding device report. CPU core comparison
uses the standalone build for both versions. Entity numbers come from the
headless engine build and have no before/after Entity comparison claim.

## Controlled results

All 18 workloads have five recorded uninstrumented samples before and after.
The checksum covers every evaluated amount, before/after version, per-write
before/after/actual/excess and final state/version. Every before/after checksum
matches, and repeated samples are deterministic. This is separate from the
randomized per-field reference tests described below.

Median core settlement latency across the six workload/semantics combinations:

| Requests | Before range, ms | After range, ms | Observed speedup range |
| ---: | ---: | ---: | ---: |
| 1,000 | 0.447–0.771 | 0.201–0.357 | 2.01–2.54× |
| 100,000 | 48.189–74.368 | 16.756–32.256 | 1.94–3.08× |
| 1,000,000 | 607.351–778.620 | 192.954–367.257 | 2.12–3.16× |

Speedup compares each identical workload, not endpoints of the ranges.
Full sample/median/maximum results are in [summary.csv](settlement-performance/summary.csv).
Million-request details (ms; throughput excludes construction and destruction):

| Distribution / reads | Before median | After median / max | Speedup | After M requests/s | Real Entity median |
| --- | ---: | ---: | ---: | ---: | ---: |
| Hotspot / Snapshot | 718.422 | 300.323 / 304.783 | 2.39× | 3.33 | 335.618 |
| Hotspot / Ordered | 730.387 | 301.685 / 303.747 | 2.42× | 3.31 | 344.588 |
| Independent / Snapshot | 608.940 | 192.954 / 204.065 | 3.16× | 5.18 | 272.177 |
| Independent / Ordered | 607.351 | 236.891 / 281.155 | 2.56× | 4.22 | 271.448 |
| Cross-dependent / Snapshot | 778.620 | 367.257 / 377.480 | 2.12× | 2.72 | 411.580 |
| Cross-dependent / Ordered | 770.953 | 332.132 / 337.380 | 2.32× | 3.01 | 378.244 |

Million-request C++ allocation calls fall from 6,000,065–6,022,103 to
2,000,017–2,001,112 (about 67%). Incremental requested C++ peak falls from
194.24–194.81 MiB to 137.33–137.56 MiB (about 29%). These peaks cover
settlement workspace **and retained output**, not the already constructed input
or process RSS. Each bank object is copied once: one hotspot state or 1,000
distributed/cross-dependent states. Input/plan scratch grows twice for the
serial group or 32 times across 16 independent task chunks, instead of twice
per request. Full result attribution still owns two allocations per ordinary
one-write request; no zero-allocation claim is made.

The one instrumented million-request sample records validation 42–45 ms,
grouping 52–78 ms, shadow copying below 0.22 ms, and core publication below
0.16 ms. Execution is 132–347 ms including output allocation, task launch/wait
and computation. Worker totals overlap in parallel workloads. The real Entity
probe records 0.005–0.27 ms identity/preflight, 0.05–15.93 ms observer/transport
publication, and 37.21–75.85 ms to dispatch a million minimal callbacks. These
are instrumented examples, not latency distributions or timings of user logic.

In the cross-dependent Ordered workload, the final neighbour retains one unit
because its source has already depleted. The recorded 99,999 total deduction
therefore differs from Snapshot's 100,000 and is preserved by optimization.
Every real Entity run checks all callbacks, per-result actual total, remaining
resource conservation and one internal publication per bank participant.

`AYStats_SettlementProfileBenchmark` measures construction, the entire synchronous
core settlement call (including internal workspace cleanup), and result/input
destruction separately. Each workload has one discarded warmup and five recorded
samples. Plan/schema compilation and scheduler startup are excluded.
The `destruction_ms` column times clearing owned request/result/state elements
and their nested allocations; outer vector capacity is released at scope exit
outside that column. Checksums are validated outside timed intervals. The maximum
of five samples is not a p95 or p99 estimate. Timings are device-specific evidence,
not an engine frame budget or a CI timing gate.

`AYStats_SettlementAllocationBenchmark` uses the same source with isolated global
C++ allocation counters. Counters reset after input construction; incremental
peak is requested live C++ bytes during settlement, excluding input payload,
allocator metadata, CRT/system allocations and process RSS. This executable adds
atomic counter and per-request clock overhead; its latency must not be compared
with uninstrumented latency. Worker evaluation/mutation times are summed work,
not additional wall-clock stages. Zero baseline stage columns mean unavailable.

`AYStats_EntitySettlementBenchmark` exercises real World/component identities,
one internal observer, state transport encoding and one public callback per
write. The callback counts deliveries and actual deductions. Entity construction
and destruction are outside the settlement interval; application-specific listener
work is not represented. Run without `profile` for latency and with `profile` for
separate preparation/publication/notification stage evidence.

Public local transactions remain atomic. Only exclusively owned, unpublished
batch shadows use the internal in-place mutation path. Each bank state is copied
once; task-local input, plan and dependency-rebuild buffers are reused. Full
per-request Settlement/Change ownership and original order are preserved.
Conflict groups are built from globally written bank objects, including later
writers. Stable scatter replaces request-pair sorting; sorted IDs replace a
heap node per request. Tasks and captures allocate before any work is submitted;
refused or throwing submissions drain accepted work before returning.

The budget conservatively estimates requested C++ workspace/result payload. It
does not guarantee a process memory cap. The scheduler must reject/throw before
accepting task ownership, and wait must complete normally. Profiling is optional
and reset on each call; failure metrics are partial, not evidence of publication.

## Reproduction and validation

From the repository root on the configured local MSVC toolchain:

```powershell
# Unique output folders; baseline also uses a unique copied source and build.
pwsh -NoProfile -File AYRuntime/AYStats/tools/benchmark-settlement.ps1 -Baseline -Samples 5
pwsh -NoProfile -File AYRuntime/AYStats/tools/benchmark-settlement.ps1 -Samples 5
pwsh -NoProfile -File AYRuntime/AYStats/tools/build-local.ps1 -BuildDirectory out/build/stats-engine -Targets AYStats_EntitySettlementBenchmark
```

Run `AYStats_EntitySettlementBenchmark 5` from the configured build's executable
directory for uninstrumented latency, and `AYStats_EntitySettlementBenchmark 1
profile` for the separate stage probe. Keep CSV lines matching `^requests,` or
`^\d+,`; World startup/entity logs are excluded from the checked-in CSVs.
Each experiment runs sequentially, with compilation and tests outside sampling.
No sleeps or hardware timing thresholds are added to correctness tests.

[before-source](settlement-performance/before-source/) contains the two captured
pre-optimization implementation files and headers reconstructed by removing only
this change's declarations. All four byte hashes match the previous
`gameplay-validation-sources.json` fingerprint. The baseline rebuild script was
run successfully; it reproduces all 18 checksums and the allocation counts
within one scheduler allocation (eight requested bytes) of the original run.
[baseline.json](settlement-performance/baseline.json) identifies the originally
measured static executables. Git's original first-version commit is an ancestor,
not the source of this newer working-tree baseline.

Current correctness acceptance: CPU-only headless full **67 cases**; full
Host/Inspector/Editor profile **70 cases**, including those headless cases;
separate real hardware GPU **3 cases**, no skips; AYCombat consumer **70 cases**.
SDK doctor: 63 capabilities, 50 modules, 12 search cases, 85 entry symbols,
zero errors/warnings. Reports and current source fingerprint are linked from
[validation.md](validation.md).

New cases compare 700 mixed requests over 37 states against public atomic local
transactions for both semantics and all three capacity policies, including
modifiers, scalar/derived-base rebuilding, derived capacity, multi-write results
and exact versions/fields/excess. Further cases cover a later writer connecting
earlier readers/read-only requests, late derived overflow with no publication,
accepted-work draining on scheduler rejection/exception, and Entity observer/
whole-bank callback ordering with profiling enabled and stale retry reset.

Next measured bottlenecks are owned result allocation/cleanup and callback
delivery; preserving every non-equivalent hit limits safe coalescing. Authored
RecipeBook resource loading/editor selection and GPU resident authoritative
settlement remain separate future work.
