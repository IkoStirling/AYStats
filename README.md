# AYStats

Independent unpublished local repository, branch `main`, no remote.

Core is C++23 standard-library-only: per-value basic/advanced attributes and pools,
explicit zones, atomic settlement, immutable intermediate plans, timed modifiers
and bounded reaction contexts. Check all `Result` values before use.

```cpp
#include <AYStats/Stats.h>
using namespace ayt::stats;
auto schema=Schema::compile({Definition{
    .name="resource",.shape=Shape::Pool,.current=100,.capacity=100}});
auto state=StatSet::create(schema.value());
Mutation consume{statId("resource"),MutationOp::Decrease,30};
auto result=state->commit(std::span(&consume,1),state->version());
```

See [design.md](design.md) and [implementation plan](../../AYDocs/AYSTATS-IMPLEMENTATION-PLAN.md).
Enable root `AY_ENABLE_STATS` (default OFF for this unpublished local checkout).
Test entry `AYStats_UnitTests` supports AYTest `--list`, filtering and JSON reports.
