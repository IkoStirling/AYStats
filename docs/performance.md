# AYStats Release performance evidence — 2026-09-29

This is an experimental device-specific baseline, not an automatic GPU threshold or a general engine frame budget. Correctness gates compare every result; wall time is reported separately.

## Environment and measurement boundary

- Windows x64, MSVC 19.51.36256 / C++23, Release `/O2`, four AYTask workers.
- CPU: AMD Ryzen 7 5800H with Radeon Graphics.
- GPU: NVIDIA GeForce RTX 3060 Laptop GPU, DXGI vendor 4318, device 9568, dedicated memory 6,285,164,544 bytes. Installed Windows display driver 32.0.16.1692, registry DriverDate 9-4-2026.
- Pure CPU and CPU/GPU executables run six times, discard first warmup, report median and maximum of the remaining five. Maximum of five samples is not p99.
- Includes input construction, owned identity/shape validation, packing, task dispatch or device upload/dispatch, wait, readback and finish. Excludes plan compilation and scheduler/device startup. Fused shader first compilation is in the discarded warmup. Includes per-call range qualification and currently repeated validation on the GPU path.
- Scalar CPU is the reference. All measured CPU/GPU outputs compare equal, including signed values and non-aligned workgroup tails in hardware tests. No approximate authoritative profile is enabled.

## Pure intermediate computation

See [raw GPU comparison](gpu-benchmark.csv); units below are milliseconds, medians.

| Requests | Simple rate CPU / GPU | 48-stage rate CPU / GPU |
| ---: | ---: | ---: |
| 1 | 0.0056 / 16.0078 | 0.0156 / 15.9625 |
| 1,000 | 0.0281 / 16.0599 | 0.7766 / 15.5257 |
| 10,000 | 0.4847 / 15.5591 | 3.7405 / 15.5576 |
| 100,000 | 4.4488 / 15.3445 | 24.8396 / 11.8020 |
| 1,000,000 | 37.4176 / 73.8992 | 256.8490 / 76.7840 |

The 48-stage recipe alternates safe rate operations in one fused compute program. This device benefits from GPU computation at large complex batches, while this simple recipe remains faster on CPU. `Automatic` therefore remains CPU unless the caller supplies a threshold measured for its recipe/device. These values cannot choose one global attack-count threshold.

Small GPU measurements include `finish` polling with a 1 ms sleep, whose actual Windows scheduling interval affects synchronous latency. They do not measure isolated kernel execution. No implicit one-frame latency is introduced.

## Complete CPU settlement

`AYStats_SettlementBenchmark` constructs an entire state bank, input bindings and requests, validates versions/read-write sets, forms conflict groups, evaluates Snapshot or Ordered semantics, commits each shadow transaction and publishes the bank. Result destruction is outside the timed interval. Four runs, discard first, median/maximum of three. Schema and plan construction and scheduler startup are excluded.

Distributions are one target hotspot; up to 1,000 independent targets; and a cycle where each target reads its neighbour. Hotspot and cross-dependent requests form one conservative group (no parallel tasks). Independent targets form up to 1,000 groups and 16 worker tasks. The resource starts at 100; excess requests remain attributable even after depletion. This benchmark does not dispatch gameplay event listeners.

See [settlement raw results](settlement-benchmark.csv) and [scalar/SIMD raw results](cpu-benchmark.jsonl). At one million requests, settlement median Snapshot/Ordered is 901.954/918.547 ms for a hotspot, 821.617/958.766 ms for independent targets and 1001.580/1049.330 ms for cross-dependent targets. Pure evaluation timings above must not be presented as complete attack or Entity settlement throughput. The current per-request shadow/settlement allocation and conservative conflict grouping are visible costs; further optimization needs the same scalar and attribution checks.

## Isolated allocation instrumentation

[MetricsBenchmark.cpp](../demo/MetricsBenchmark.cpp) overrides C++ new/delete only in a separate executable. It records actual accepted scheduler submissions, new calls and peak incremental outstanding requested C++ bytes above the warm baseline. Metadata/allocator overhead, malloc calls inside system/driver DLLs and GPU memory are excluded. Instrumentation adds overhead, so these timings are not substituted into the uninstrumented comparison. One recorded warm run per backend is diagnostic evidence, not a latency distribution.

At one million simple-rate requests, [raw metrics](allocation-metrics.csv) report:

| Backend | Construct inputs | Submit / validate / pack | Finish / wait / readback | C++ new calls | Incremental C++ peak | Tasks / SIMD lanes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU 4 workers | 18.042 ms | 15.7473 ms | 3.5983 ms | 1,312 | 89,030,255 bytes | 245 / 1,000,000 |
| GPU direct warm submit | 16.762 ms | 22.5181 ms | 1.9556 ms | 23 | 80,000,707 bytes | 0 / 0 |

CPU tasks execute while submit is still running; finish is remaining wait, not total CPU compute time. GPU submit includes qualification, packing, input UpdateSubresource and commands; finish includes synchronization, Map and int32-to-int64 output widening. Direct GPU submit owns and moves the input once, while the selector comparison above retains a fallback copy and performs additional qualification, explaining different timings. The warm GPU path reuses a previously consumed exact-shape buffer set and cached shader. Those persistent caches are in the baseline rather than the incremental peak.

## Budgets and mechanical evidence

- CPU evaluation defaults: at most 1,000,000 lanes, 4,096 tasks, 256 detailed diagnostics and 256 MiB estimated temporary memory. Caller input construction is separate from the backend's budget. The estimate is conservative admission control, not a measured allocator peak.
- Settlement defaults: at most 1,000,000 requests, 65,536 bank entries, 4,000,000 operations and 256 MiB estimated temporary memory. Over-budget work is rejected before live publication; no silent drop.
- Two-input strict32 GPU computation transfers `12 × lane count` bytes (8 input + 4 output); at one million lanes this is 12,000,000 bytes. Device input/output/staging storage admission estimate is `20 × lane count`; contexts and int64 CPU results are additional host memory. One batch uses one fused dispatch and one completion query/readback, not one dispatch per stage.
- Shader cache: at most 64 programs; idle buffer pool: at most four exact-shape buffer sets and 128 MiB. Returned, consumed jobs can reuse buffers; unfinished jobs retain their resources and are not recycled.
- Mixed CPU stress fixture compares 32 heterogeneous recipes × 2,051 lanes (65,632 total), including table/random/complex plans, across 1/4 workers and block sizes 31/4,096.
- Hardware equivalence fixture checks 1/257/10,003 lanes, strict32 integer boundaries, qualified/refused profiles, CPU fallback, cancellation and device/job lifetime. GPU is a separately requested test tier; a device skip never counts as hardware acceptance.

## Limits of this baseline

Detailed device upload/kernel/readback GPU timestamp separation, settlement allocator counts, snapshot/cache-hit counters, event callback costs, GPU device-loss injection, offline summary output and the full cache-thrashing workload matrix are not instrumented in this baseline. Their absence is explicit; end-to-end timings include actual synchronization and transfers. Reported admission budgets/cache bounds must not be relabelled as measured peaks or hit rates; the separate C++ allocation measurement has the boundary above. Expand production telemetry and workloads before adopting a GPU policy or a hard millisecond CI threshold. This first version supports per-request owned outputs and CPU-authoritative publication, not GPU-resident shared state or device-side settlement.

## Reproduce

Build the excluded benchmark targets with `tools/build-local.ps1 -Targets AYStats_Benchmark,AYStats_GpuBenchmark,AYStats_SettlementBenchmark,AYStats_MetricsBenchmark` using a Release configuration and optional GPU enabled. Run CPU counts `1`, `1000`, `10000`, `100000`, `1000000`; run the GPU executable without arguments for both recipes or with `--device` for DXGI information. Run settlement without arguments for all count/distribution/semantics combinations; run isolated metrics with `1000000`. Keep the machine idle during a comparison and retain raw samples and build configuration. Run `scripts/tests/run-module-tests.ps1 -Module AYStats -Tier full` and separately `-Tier gpu` before comparing timings.
