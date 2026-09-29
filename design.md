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
