#include <AYTest.h>
#include <AYStats/Recipes.h>
#include <AYStats/Effects.h>
using namespace ayt::stats;
namespace {
Recipe recipe(std::string name="cost",bool isDefault=true) {
    return {std::move(name),statId("mana"),AttributeOp::Decrease,
        CalculationPlan::compile({{.id=1,.op=Op::Input,.binding=0}},1,1).value(),{{InputSource::Amount}},isDefault};
}
}
TEST_SUITE(StatsRecipeTests)
TEST_CASE(incompatible_explicit_recipe_never_falls_back_to_a_default) {
    auto book=RecipeBook::compile({recipe()}).value();
    CHECK_TRUE(book->select(statId("mana"),AttributeOp::Decrease).value()->name=="cost");
    CHECK_TRUE(book->select(statId("rage"),AttributeOp::Decrease).value()==nullptr);
    CHECK_TRUE(book->select(statId("mana"),AttributeOp::Increase,"cost").error().code==Error::Invalid);
    CHECK_TRUE(book->select(statId("rage"),AttributeOp::Decrease,"cost").error().code==Error::Invalid);
    CHECK_TRUE(book->select(statId("mana"),AttributeOp::Decrease,"missing").error().code==Error::Missing);
}
TEST_CASE(configuration_rejects_ambiguous_defaults_names_and_input_shapes) {
    CHECK_TRUE(RecipeBook::compile({recipe("one"),recipe("two")}).error().code==Error::Duplicate);
    CHECK_TRUE(RecipeBook::compile({recipe("one",false),recipe("one",false)}).error().code==Error::Duplicate);
    auto bad=recipe();bad.inputs.clear();CHECK_FALSE(RecipeBook::compile({bad}).has_value());
    bad=recipe();bad.inputs={{InputSource::SourceStat,0}};CHECK_FALSE(RecipeBook::compile({bad}).has_value());
    bad=recipe();bad.plan=CalculationPlan{};CHECK_FALSE(RecipeBook::compile({bad}).has_value());
}
TEST_CASE(pool_fields_require_pool_and_missing_source_is_a_diagnostic) {
    auto schema=Schema::compile({{.name="power",.initial=20}}).value();auto state=StatSet::create(schema).value();
    auto r=recipe();r.inputs={{InputSource::SourceStat,statId("power")}};
    CHECK_TRUE(sampleRecipe(r,20,state).error().code==Error::Missing);
    r.inputs={{InputSource::TargetStat,statId("power"),InputField::Current}};
    CHECK_TRUE(sampleRecipe(r,20,state).error().code==Error::Invalid);
    r.inputs={{InputSource::TargetStat,statId("power"),InputField::Base}};
    CHECK_TRUE(sampleRecipe(r,20,state).value()[0]==20);
}
TEST_CASE(scalar_increase_checked_arithmetic_preserves_state_on_overflow) {
    auto schema=Schema::compile({{.name="wide",.initial=INT64_MAX}}).value();auto state=StatSet::create(schema).value();
    CHECK_TRUE(attributeMutation(state,statId("wide"),AttributeOp::Increase,1).error().code==Error::Overflow);
    CHECK_TRUE(state.read(statId("wide"))->base==INT64_MAX);
    const Mutation zero{statId("wide"),MutationOp::SetBase,0};const auto version=state.version();
    const auto changed=state.commit(std::span(&zero,1),version).value();
    CHECK_TRUE(changed.changes[0].actual==-INT64_MAX);
    CHECK_TRUE(state.version()==version+1 && state.read(statId("wide"))->base==0);
}
TEST_CASE(scalar_set_obeys_declared_range_and_negative_changes_do_not_invert_operation) {
    auto schema=Schema::compile({{.name="power",.initial=5,.maximum=100}}).value();
    auto state=StatSet::create(schema).value();const auto version=state.version();
    auto negative=attributeMutation(state,statId("power"),AttributeOp::Set,-1).value();
    CHECK_TRUE(state.commit(std::span(&negative,1),version).error().code==Error::Invalid);
    CHECK_TRUE(state.version()==version && state.read(statId("power"))->base==5);
    auto zero=attributeMutation(state,statId("power"),AttributeOp::Set,0).value();
    const auto changed=state.commit(std::span(&zero,1),version).value();
    CHECK_TRUE(changed.changes[0].actual==-5);CHECK_TRUE(state.read(statId("power"))->base==0);
    CHECK_TRUE(attributeMutation(state,statId("power"),AttributeOp::Decrease,-1).error().code==Error::Invalid);
}
TEST_CASE(effect_removal_preserves_unowned_same_effect_contributions) {
    auto schema=Schema::compile({{.name="power",.mode=Mode::Advanced,.initial=20,.zones={{1,ZoneOp::Flat}}}}).value();
    auto state=StatSet::create(schema).value();EffectManager effects;
    Effect owned{1,1,UINT64_MAX,1,{{.handle=1,.rule=1,.target=statId("power"),.zone=1,.value=10}}};
    CHECK_TRUE(effects.apply(state,owned,EffectPolicy::Independent,0).has_value());
    Modifier external{.handle=2,.effect=1,.rule=2,.target=statId("power"),.zone=1,.value=5};CHECK_TRUE(state.insert(external).has_value());
    CHECK_TRUE(effects.remove(state,1).has_value());CHECK_TRUE(state.read(statId("power"))->effective==25);
    CHECK_TRUE(state.modifiers().size()==1 && state.modifiers()[0]==external);
}
TEST_SUITE_END
