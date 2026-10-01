# Gameplay entry points

This follow-up supplies owner-side simple actions and immutable named recipes. Link
`AYStats::Entity`, register `StatsIntegrationModule` before the component registry
is sealed, and install one `StatsSystem` in the gameplay World. Host/Scene startup
can do this through the existing Stats Host module. Check every returned Result.

```cpp
auto* stats = dynamic_cast<ayt::stats::StatsSystem*>(world.findSystemByName("StatsSystem"));
if (!stats) throw std::runtime_error("StatsSystem is not installed");
auto consumed = stats->decrease(target, "resource", 30);
if (!consumed) throw std::runtime_error(consumed.error().message);
// Pool: actual current reduction, clipped at zero, with explicit excess.
auto strength = stats->increase(target, "strength", 5);
if (!strength) throw std::runtime_error(strength.error().message);
// Scalar: base increases by 5, then its authored zones rebuild effective value.
```

`set` obeys the authored range; it does not clamp silently. Persistent values remain
non-negative in this profile. Increase/Decrease amounts and calculated outputs must
be non-negative; negative damage does not become healing. Pure intermediate
calculations support signed int64. Amounts use integer units, rates use basis points.

## Recipe selection and ownership

Compile `RecipeBook` once from trusted host configuration, or load a versioned
`.stats-recipes.json` file with `StatsSystem::loadRecipes(path, targetSchema,
sourceSchema)`. Each `Recipe` names its
target StatId, AttributeOp, immutable CalculationPlan and ordered RecipeInput bindings.
Bindings explicitly distinguish Amount/Constant/SourceStat/TargetStat and
Effective/Base/Current/Capacity. Current and Capacity require a pool. Binding indices
must match the plan's input count. Recipe names are unique; each target/operation can
have at most one default. Caps are 4,096 recipes and 64 inputs per recipe.

Install the shared book with `StatsSystem::setRecipes`, or let `loadRecipes`
validate the whole file and replace it atomically. A failed read or validation
leaves the previous book active. The file records the named recipes, target and
operation, default flag, ordered input provenance, and inline plan. SourceStat
bindings require the source schema at load time. An explicit name must exist
and match the target and operation. An omitted name selects the authored default;
without a default it uses direct amount. To preserve both behaviors for one operation,
give the complex recipe an explicit name and leave it non-default, or author a named
direct-input recipe alongside a complex default. Source handles are passed as the
last optional parameter of increase/decrease/set/change. They must resolve to
configured authority state in the same World.

`prepareCalculation` returns an owned plan and sampled pure inputs, not a commit
ticket. Evaluation survives recipe replacement and original World destruction.
The source/target lifecycle must be checked separately before any delayed publication.
For async work retain World token, EntityHandle, component generation and state
version, and submit through `settleEntityBatch`. Runtime restore/configure changes
component generation even when a saved numeric version is identical.

## Batch semantics

`StatsSystem::changeBatch` takes named AttributeActions and captures participant
identities/versions on the World owner. It delegates to the same immutable recipe
translation and atomic bank settlement as the low-level API. Action IDs must be
nonzero and unique within a submission; calling again is a new command. Cross-call
business deduplication belongs to the gameplay layer, not an ever-growing Stats log.

Ordered is the default: each request reads the evolving shadow, so deterministic
recipes match consecutive single actions. Scalar increments also use that evolving
base; they never precompute several SetBase writes against the original base.
Snapshot reads the original bank but still attributes actual writes in request order.
It is useful for coordinated outputs of one hit that must share the original shield.
Independent targets can use the supplied engine ITaskScheduler; all state access is
declared, so a shared read-only source does not serialize every target. Shared writes
and cross dependencies connect conservative serial groups. Any evaluation/validation
failure discards the entire bank before public notifications.

Single and high-level batch calls allocate fresh calculation identities. Comparing
independent calls with Random nodes requires an explicitly reproduced random context;
use the lower-level plan/request APIs for replay. Do not expect different action
identities to draw identical random values. This facade does not replace the CPU
settlement optimization work or move shared mutable state onto the GPU.

## Notifications and gameplay boundaries

`StateObserver` weak registration is independent of the public settlement callback.
prepare validates capacity/schema before a numeric publication; rejection or exception
leaves numeric state unchanged. published propagates Commit/Effects/Configure/
Restore/Replica/Detach synchronously and must be allocation-free/noexcept. Observers
must not mutate owners, World or registration. Expired registrations are ignored and
reclaimed on registration. Detach is an infallible removal notification, without a
vetoing preflight. Configure/restore are incarnation changes rather than
ordinary gameplay resource events.

`setOnSettled` observes committed actual changes. Batch callbacks see the entire
published bank; direct reentry is rejected, including another batch participant.
Queue numeric commands for the next tick when needed. Owner/World destruction inside
callbacks is outside the contract. Effect update/remove uses the instance ID and
preserves contributions from other same-key sources.

Stats operations themselves have no targeting, team, damage permission or death
policy. AYCombat owns those decisions. The demo interprets health reaching zero and
chooses actual health loss as leech basis at the gameplay layer; those rules are not
hard-coded into Core or the SDK catalog.

## Runnable example

Build `AYStats_GameplayExample` from an engine profile with Stats enabled and Entity
integration present. The executable defaults to its `demo/assets` directory and
saves runtime checkpoints under the system temporary directory. Optionally pass
`<assets-directory> <save-directory>` as its two arguments. The example is excluded
from default builds and is also exercised by `StatsGameplayTests`.

It loads authored definitions and a complete recipe-book JSON file,
applies flat +20, summed +50%, and independent ×1.1 to attack 100. Effective attack
is 198; a 50% hit yields 99. The shared snapshot lets shield absorb 30 and health
request 69. With 50 health left, actual health loss is 50 and excess is 19. A 20%
leech recipe heals 10. Saves/reloads preserve source 50, target 0 and owned buff;
the buff expires after three fixed ticks, returning attack to 100. The example
checks that pre-restore identities cannot publish late results.

Loose definition/plan/recipe-book JSON is supported. The Stats Inspector can
load a recipe file under the project's Assets directory, optionally load a
source definitions file, select a named recipe, preview exact inputs, and
validate/save the complete document. Recipe file saving checks that the
selection and previous file text still match. ResourceManager/cook/pak
integration and dedicated delta network transport are future work.
