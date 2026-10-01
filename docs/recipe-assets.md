# Recipe-book assets

`StatsSystem::loadRecipes(path, targetSchema, sourceSchema)` reads a complete,
versioned loose JSON file, validates every named recipe against the supplied
schemas, compiles an immutable `RecipeBook`, and publishes it only after success.
A read, version, schema, plan, duplicate recipe name, or duplicate default failure leaves the installed book
unchanged. `sourceSchema` may be omitted only if no input uses `source-stat`.

The file uses `kind: "stats-recipes"`, `version: 1`, and a `recipes` array. Each
recipe has exactly `name`, `target`, `operation`, `default`, `inputs`, and `plan`.
`operation` is `increase`, `decrease`, or `set`; `plan` is an inline versioned
`stats-plan` document. Inputs are ordered to match the plan's input bindings:

```json
{"version":1,"kind":"stats-recipes","recipes":[{"name":"direct-hit","target":"health","operation":"decrease","default":true,"inputs":[{"source":"amount"}],"plan":{"version":1,"kind":"stats-plan","inputs":1,"output":1,"nodes":[{"id":1,"op":0,"a":0,"b":0,"c":0,"binding":0,"value":0,"table":[],"extension":""}]}}]}
```

Input sources are `amount`, `constant` (with integer `value`), `source-stat`,
and `target-stat` (both with `stat` and `field`). Fields are `effective`,
`base`, `current`, and `capacity`; the last two require a pool. Target names
must resolve to persistent attributes. Recipe names and default
target/operation pairs are unique. The decoder caps the file at 16 MiB, the
book at 4,096 recipes, and each recipe at 64 bindings. Plans retain their own
node and operand validation. Use `encodeRecipeBook` to produce canonical JSON.

In the Stats Inspector, enter a path relative to the project's `Assets`
directory. For source-stat bindings, also enter the path to a versioned source
definitions JSON file. Load validates the book and fills the recipe picker and
document editor. The preview evaluates the selected plan with explicit CSV
integers and does not mutate a World. Save validates the edited whole document,
then the Editor checks selection/generation and the original on-disk text
before atomic replacement. Recipe files are independent project assets, so
this file save does not join scene-document undo history. Reload after changing
the source definitions path. This flow does not register recipes in
ResourceManager directly. For runtime use, add a separate `.aystats.json` source
manifest under `Assets`:

```json
{"version":1,"kind":"stats-recipe-source","targetDefinitions":"target.stats.json","sourceDefinitions":"source.stats.json","recipeBook":"combat.stats-recipes.json"}
```

All references are relative to `Assets`; use an empty `sourceDefinitions` when
the book has no source-stat binding. ProjectBuild validates all referenced files,
requires them in the build profile, hashes their contents with the manifest, and
cooks a self-contained `.aystats` asset at the same relative path. Select that
runtime path in a scene's `StatsRecipeComponent.assetPath`. One active component
owns the recipe book per World. Loading works from a loose cooked file or a
mounted DB/Pak, and invalid replacement leaves the prior book active. The
Inspector continues editing the original recipe JSON; recook to update shipped
content. See [the example manifest](../demo/assets/combat.aystats.json).

In AYEditor, **File > New AYStats** creates a project asset under `Assets/stats`:
a starter target definition (`value` pool), a default amount-based decrease
recipe, a `.aystats.json` source manifest, and a cooked `.aystats` preview.
The resource browser indexes these files; no manual copying is required.
Alternatively, add `Gameplay / Stats Recipes` to a Scene entity and click
**Create new AYStats and attach** in its Inspector to create the same asset set
and attach its runtime path in one step. The Inspector can also choose an
existing `.aystats`, or build a `.aystats.json` from existing target definitions,
optional source definitions, and recipe-book files. The editor
validates and cooks a loose preview resource, then records the logical path in
the Scene document so attach/undo/redo/save work normally. Saving the recipe
book through the Stats Inspector rebuilds referring loose recipe assets and
requests resource reload; the World switches to the new book on its next fixed
Sim update. The project build still performs its own cook for distribution.

The editor regression in `unittest/Test_Editor.cpp` exercises the Inspector's
input and button widgets: an invalid recipe-book path writes neither manifest
nor cooked asset; a valid path builds and attaches; one-click creation generates
and indexes a distinct starter asset set, and the Scene reference survives
undo/redo. `unittest/RecipePackagingTests.cpp` includes a valid Scene
reference in the build plan, then verifies Pak-only recipe loading after
removing the source Assets directory. A desktop smoke check confirmed that
Gameplay / Stats Recipes can be added to an entity and that the Inspector
controls remain reachable by scrolling. Manual text entry, saving and
reopening a desktop project, and Play still need a full product-level check.

The runnable [combat example](../demo/assets/combat.stats-recipes.json) uses
the same format with different target and source schemas.
