# AYStats v0.1 contracts

2026-09-29. Execution: root `AYDocs/AYSTATS-IMPLEMENTATION-PLAN.md`.

## Numeric profile

Signed 64-bit integer amounts; rates/factors use 10000 units per 1.0. Each
declared operation rounds toward zero. Multiply/divide forms an exact unsigned
128-bit magnitude product; a quotient outside signed 64-bit range fails. Addition
overflow fails. No implicit saturation, negative damage-to-healing conversion or
product reordering. Persistent values are non-negative with declared ranges;
intermediate calculations may be signed. Division by zero names its failing stage.

## Definitions, identity and lifetime

Each value independently selects scalar/pool, basic/advanced and persistent/
intermediate. Advanced pool capacity references a scalar; current quantity is
independent. Zero authored current remains zero, including on startup. Runtime
mode conversion is unsupported. Schema validates stable FNV names, collisions,
ranges and acyclic scalar dependencies. Mutable state has one simulation owner;
workers consume immutable snapshots. Versions increase only on successful writes.

Modifier identity is `(effect,rule,hit,layer,target,stage)`, not just source.
Unique handles remove exact instances. Products and override ties use stable
identity order, independent of registration order. Zone execution follows author
order: Flat sum, PercentSum with neutral 10000, FactorProduct, Minimum/Maximum,
Override (highest priority then first stable identity). Final clamp is explicit.
Effects removed by handle trigger recomputation rather than inverse arithmetic.

## Plans and settlement

Node ids identify intermediate stages. All author nodes are validated, then only
the output dependency closure executes. Select is eager: invalid hidden operands
still fail. C++ extensions use stable registered ids, are pure/thread-safe and
CPU-only. Random domains: seed/tick/calculation/hit/purpose/draw; bounded draws
use rejection sampling, never a shared worker RNG.

Local transactions validate on shadow state before publication. Resource changes
preserve each operation's actual delta and excess. Version mismatches fail before
writes. Capacity policies: KeepCurrent, KeepRatio, KeepMissing; zero old capacity
under KeepRatio retains/clamps current. Cross-object atomic groups require
owner-controlled shadow grouping. Events follow commit, never reenter it.

## Effects and reactions

Effects have half-open integer-tick lifetimes `[applied,expires)`; expiration sorts
by `(expires,id)`. Refresh preserves modifiers and updates expiry, Replace removes
matching keys, Stack admits separate instances up to declared count. State and
manager publish together. Reactions record parent/depth and enforce total/depth
budgets; denial and overflow are errors, not silent drops. Game-specific death,
reflection and targeting are outside Core. Implicit previous-frame feedback is
unsupported.

## Batch/backend contract

Snapshot and ordered evaluation are explicit, distinct semantics. Ordered
health-dependent requests re-evaluate after earlier commits. Expected intra-batch
writes advance conflict-group versions; external changes invalidate input.
CPU scalar is the reference. Every backend preserves precision, operand order,
identity mapping, random domain and actual settlement attribution. Strict GPU
requires bit equality; unsupported profiles fall back only in automatic mode.
No implicit one-frame latency. GPU is an optional dependency, HEADLESS stays CPU.

## First-version implementation and limits

Schema admits 65,536 definitions, 256 zones per value and dependency depth 256;
plans admit 4,096 inputs/nodes and depth 256. Modifier cap is 65,536. BatchOptions
and SettlementLimits expose request/task/temporary-memory limits. They estimate
backend storage, not caller construction or measured allocator peak. SIMD only
specializes qualified simple rate plans; generic signed/widened arithmetic stays
exact. Settlement declares all state access, preserves original request order
within conservative conflict groups, validates external versions and publishes
the whole bank only after successful shadow work.

GPU is optional D3D11 hardware FL11 with signed32 interval proof for every
intermediate and product before rate division. Random/Table/Extension stay CPU.
Count/input/instruction/resource caps are one million/256/256/128 MiB. Fused shader
cache is bounded to 64 programs; returned idle buffers to four sets / 128 MiB.
Automatic requires a measured recipe/device threshold; default CPU. Single-thread
device/context ownership covers submit/ready/finish/resource reuse. Cancel stops
delivery, not submitted commands; finish defaults to a bounded ten-second wait.
Device/readback failure occurs before state publication. Runtime HLSL compilation
uses d3dcompiler; cross-platform/offline shader publishing remains future scope.

AssetIO separates author definitions, runtime checkpoint and network wire state.
Runtime restore checks schema, ranges, versions and effect handle ownership and
rebuilds derived caches atomically. JSON bounds include 16 MiB text and depth 32.
Loose file replacement is atomic; this is not a ResourceManager/cook/pak pipeline.
Author field edit and recipe preview are pure. Runtime mode conversion is absent.

Entity integration registers before seal and installs one StatsSystem per World.
Only fixed Sim advances the integer tick. Scene/Host integration installs subsequent
edit/play Worlds and disconnects before Host service destruction. Batch entry checks
World token, EntityHandle, component generation, version and authority. Reset/restore
changes incarnation and drops queued commands/events. Replica state applies at Sim,
never local authority writes. Script uses decimal strings for int64 and borrows World;
unbind before destruction. Network sends complete runtime state, with separately
provisioned schema. Legacy conversion explicitly chooses one authority and does not
alter the old Health component's independent startup behavior.

Callbacks run after publication and cannot destroy owners or bypass the mutation
boundary. Reentrant mutation/configuration/effects fail; enqueue defers commands to
the next captured fixed-tick queue. Observer exceptions are post-commit diagnostics.
Inspector uses independent preview and exact fields; Editor publication checks live
selection, handle/generation, old text and Edit mode, then existing undo commands.
Full interactive smoke and finer performance telemetry are documented separately
from the reproducible functional and end-to-end hardware evidence in docs/performance.md.

## Entity notification delivery used by Combat

StatsComponent::commit accepts NotificationDelivery::Immediate (existing default)
or NextTick. NextTick still commits and publishes authority state synchronously;
only the observer delivery is deferred until the next Stats Sim update. It reserves
a bounded 1024-entry queue and the per-settlement change storage before mutation.
Overflow fails before mutation; configure/restore discard old incarnation events.
Combat uses this entry to finish hit confirmation, life and result retention before
observers run. Observer exceptions are post-commit diagnostics and do not turn a
successful settlement into retryable work. Real integration evidence is in
AYCombat/unittest/Test_EntityIntegration.cpp (ordering, failure, and backpressure).
