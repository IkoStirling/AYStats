#include <AYTest.h>
#include <AYTestFixtures.h>
#include <AYStats/AssetIO.h>
#include <AYStats/RecipeAsset.h>
#include "Fixtures.h"
#include <array>
#include <stdexcept>
#include <filesystem>
#include <chrono>
namespace {
std::string replaced(std::string text,std::string_view before,std::string_view after) {
    const auto position=text.find(before);
    if(position==std::string::npos)throw std::runtime_error("test recipe fixture field missing");
    text.replace(position,before.size(),after);return text;
}
}
TEST_SUITE(StatsAssetTests)
TEST_CASE(recipe_authoring_creates_cooks_and_recooks_linked_assets) {
    namespace fs=std::filesystem;
    const auto root=ayt::test::testTmpDir()/("stats-author-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"recipes");
    auto schema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=50,.capacity=100}}).value();
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value();
    auto initial=RecipeBook::compile({{"hit",statId("health"),AttributeOp::Decrease,plan,
        {{InputSource::Amount}},true}}).value();
    const auto definitions=root/"recipes/target.stats.json";
    const auto book=root/"recipes/hit.stats-recipes.json";
    const auto manifest=root/"recipes/hit.srs";
    CHECK_TRUE(writeTextAtomic(definitions,encodeDefinitions(*schema).value()).has_value());
    const auto original=encodeRecipeBook(*initial,*schema).value();
    CHECK_TRUE(writeTextAtomic(book,original).has_value());
    auto created=saveRecipeAssetSource(manifest,root,"recipes/target.stats.json","",
        "recipes/hit.stats-recipes.json");
    if (!created) throw std::runtime_error(created.error().message + " at " + created.error().path);
    CHECK_TRUE(created->logicalPath=="recipes/hit.sts");
    const auto output=root/"recipes/hit.sts";
    CHECK_TRUE(decodeRecipeAsset(readText(output).value()).has_value());
    CHECK_FALSE(saveRecipeAssetSource(manifest,root,"recipes/target.stats.json","",
        "recipes/hit.stats-recipes.json").has_value());
    auto changed=RecipeBook::compile({{"heavy",statId("health"),AttributeOp::Decrease,plan,
        {{InputSource::Amount}},true}}).value();
    const auto replacement=encodeRecipeBook(*changed,*schema).value();
    auto rebuilt=saveRecipeBookAndRecook(book,root,original,replacement);
    CHECK_TRUE(rebuilt.has_value() && rebuilt->size()==1 && rebuilt->front()==output);
    auto asset=decodeRecipeAsset(readText(output).value());CHECK_TRUE(asset.has_value());
    CHECK_TRUE(asset->book->select(statId("health"),AttributeOp::Decrease,"heavy").has_value());
    CHECK_FALSE(saveRecipeBookAndRecook(book,root,original,original).has_value());
    CHECK_FALSE(saveRecipeBookAndRecook(book,root,replacement,"{broken").has_value());
    CHECK_TRUE(readText(book).value()==replacement);
    CHECK_TRUE(decodeRecipeAsset(readText(output).value())->book->select(
        statId("health"),AttributeOp::Decrease,"heavy").has_value());
    CHECK_FALSE(saveRecipeAssetSource(root/"escape.srs",root,"../outside.json","",
        "recipes/hit.stats-recipes.json").has_value());
    fs::remove_all(root);
}
TEST_CASE(recipe_asset_source_closure_and_self_contained_roundtrip) {
    namespace fs=std::filesystem;
    const auto root=fs::path(__FILE__).parent_path().parent_path()/"demo/assets";
    auto source=inspectRecipeAssetSource(root/"combat.srs",root);
    CHECK_TRUE(source.has_value());
    CHECK_TRUE(source->logicalPath=="combat.sts");
    auto cooked=encodeRecipeAsset(source->asset);
    CHECK_TRUE(cooked.has_value());
    auto loaded=decodeRecipeAsset(*cooked);
    CHECK_TRUE(loaded.has_value());
    CHECK_TRUE(loaded->book->select(statId("health"),AttributeOp::Decrease,"spill").has_value());
    CHECK_FALSE(decodeRecipeAsset("{\"version\":2}").has_value());
    const auto outside=inspectRecipeAssetSource(root/"combat.srs",root.parent_path()/"unittest");
    CHECK_FALSE(outside.has_value());
}
TEST_CASE(recipe_book_roundtrip_retains_authored_provenance_and_defaults) {
    auto targetSchema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=40,.capacity=100}}).value();
    auto sourceSchema=Schema::compile({{.name="attack",.initial=30}}).value();
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Input,.binding=1},
        {.id=3,.op=Op::MultiplyRate,.a=1,.b=2}},3,2).value();
    auto book=RecipeBook::compile({
        {"strike",statId("health"),AttributeOp::Decrease,plan,
         {{InputSource::SourceStat,statId("attack")},{InputSource::Constant,0,InputField::Effective,15000}},true},
        {"direct",statId("health"),AttributeOp::Decrease,
         CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value(),{{InputSource::Amount}},false}
    }).value();
    auto text=encodeRecipeBook(*book,*targetSchema,sourceSchema.get());CHECK_TRUE(text.has_value());
    auto restored=decodeRecipeBook(*text,*targetSchema,sourceSchema.get());CHECK_TRUE(restored.has_value());
    CHECK_TRUE(encodeRecipeBook(**restored,*targetSchema,sourceSchema.get()).value()==*text);
    auto selected=(*restored)->select(statId("health"),AttributeOp::Decrease).value();
    CHECK_TRUE(selected && selected->name=="strike");
    auto target=StatSet::create(targetSchema).value(),source=StatSet::create(sourceSchema).value();
    auto inputs=sampleRecipe(*selected,0,target,&source).value();
    CHECK_TRUE(inputs==std::vector<Value>({30,15000}));
    std::vector<Value> scratch(selected->plan.instructions().size());
    CHECK_TRUE(selected->plan.evaluate(inputs,{},scratch).value()==45);
    CHECK_TRUE((*restored)->select(statId("health"),AttributeOp::Decrease,"direct").value()->name=="direct");
}
TEST_CASE(recipe_book_document_rejects_bad_contract_without_fallback) {
    auto targetSchema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=40,.capacity=100},
                                       {.name="power",.initial=10}}).value();
    auto sourceSchema=Schema::compile({{.name="attack",.initial=30}}).value();
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value();
    auto book=RecipeBook::compile({
        {"strike",statId("health"),AttributeOp::Decrease,plan,{{InputSource::SourceStat,statId("attack")}},true},
        {"alternate",statId("health"),AttributeOp::Decrease,plan,{{InputSource::TargetStat,statId("power")}}}
    }).value();
    const auto valid=encodeRecipeBook(*book,*targetSchema,sourceSchema.get()).value();
    const auto rejected=[&](std::string text,Error error){auto result=decodeRecipeBook(text,*targetSchema,sourceSchema.get());CHECK_TRUE(!result && result.error().code==error);};
    rejected(replaced(valid,"\"version\":1","\"version\":2"),Error::Invalid);
    rejected(replaced(valid,"\"kind\":\"stats-recipes\"","\"kind\":\"stats-plan\""),Error::Invalid);
    rejected(replaced(valid,"\"target\":\"health\"","\"target\":\"missing\""),Error::Missing);
    rejected(replaced(valid,"\"operation\":\"decrease\"","\"operation\":\"damage\""),Error::Invalid);
    rejected(replaced(valid,"\"source\":\"source-stat\"","\"source\":\"unknown\""),Error::Invalid);
    rejected(replaced(valid,"\"name\":\"alternate\"","\"name\":\"strike\""),Error::Duplicate);
    rejected(replaced(valid,"\"default\":false","\"default\":true"),Error::Duplicate);
    rejected(replaced(valid,"\"field\":\"effective\"","\"field\":\"current\""),Error::Invalid);
    rejected(replaced(valid,"\"inputs\":1","\"inputs\":2"),Error::Invalid);
    rejected(replaced(valid,"\"source\":\"source-stat\"","\"source\":\"source-stat\",\"unknown\":1"),Error::Invalid);
    CHECK_TRUE(decodeRecipeBook(valid,*targetSchema).error().code==Error::Missing);
    CHECK_TRUE(decodeRecipeBook(valid,*targetSchema,sourceSchema.get()).has_value());
}
TEST_CASE(definitions_and_runtime_roundtrip_keep_zero_health) {
    auto set=fixture();CHECK_TRUE(set.commit(std::array{Mutation{statId("health"),MutationOp::SetCurrent,0}},set.version()).has_value());
    auto encoded=encodeDefinitions(*set.schema());auto decoded=decodeDefinitions(*encoded);CHECK_TRUE(decoded.has_value());
    auto state=decodeState(encodeState(set).value(),*decoded);CHECK_TRUE(state.has_value());CHECK_TRUE(state->read(statId("health"))->current==0);CHECK_TRUE(state->version()==set.version());
    auto definitions=std::vector<Definition>((*decoded)->definitions().begin(),(*decoded)->definitions().end());definitions[0].stage++;
    CHECK_TRUE(decodeState(encodeState(set).value(),Schema::compile(definitions).value()).error().code==Error::Stale);
}
TEST_CASE(timed_effect_checkpoint_preserves_expiry_and_random_domain) {
    auto set=fixture();EffectManager manager;CHECK_TRUE(manager.apply(set,{.id=1,.key=5,.expires=20,.modifiers={modifier(1,1,20)}},EffectPolicy::Independent,4).has_value());
    RuntimeCheckpoint saved{set,manager,{.seed=123,.tick=4,.calculation=5}};auto checkpoint=decodeCheckpoint(encodeCheckpoint(saved).value(),set.schema());CHECK_TRUE(checkpoint.has_value());
    CHECK_TRUE(checkpoint->effects.tick()==4);CHECK_TRUE(randomBits(checkpoint->context,7)==randomBits(saved.context,7));
    CHECK_TRUE(checkpoint->effects.advance(checkpoint->state,19).has_value());CHECK_TRUE(checkpoint->state.read(statId("attack"))->effective==120);
    CHECK_TRUE(checkpoint->effects.advance(checkpoint->state,20).has_value());CHECK_TRUE(checkpoint->state.read(statId("attack"))->effective==100);
}
TEST_CASE(malformed_version_and_unsigned_overflow_are_rejected) {
    CHECK_FALSE(decodeDefinitions("{}").has_value());auto text=encodeDefinitions(*fixture().schema()).value();
    auto position=text.find("\"version\":1");text.replace(position,11,"\"version\":9");CHECK_FALSE(decodeDefinitions(text).has_value());
    text=encodeDefinitions(*fixture().schema()).value();position=text.find("\"initial\":100");text.replace(position,13,"\"initial\":18446744073709551615");CHECK_FALSE(decodeDefinitions(text).has_value());
}
TEST_CASE(atomic_file_write_and_failed_read_leave_owner_unchanged) {
    const auto directory=ayt::test::testTmpDir()/"stats-assets";std::filesystem::create_directories(directory);const auto path=directory/"checkpoint.json";
    auto set=fixture();const auto encoded=encodeState(set).value();CHECK_TRUE(writeTextAtomic(path,encoded).has_value());CHECK_TRUE(readText(path).value()==encoded);
    CHECK_TRUE(readText(path,1).error().code==Error::Limit);CHECK_FALSE(writeTextAtomic(directory/"missing"/"bad.json",encoded).has_value());CHECK_TRUE(readText(path).value()==encoded);
}
TEST_CASE(plan_roundtrip_keeps_stage_ids) {
    auto restored=decodePlan(encodePlan(damagePlan()).value());CHECK_TRUE(restored.has_value());
    const std::array<Value,2> inputs{120,15000};CHECK_TRUE(trace(*restored,inputs,{})->result==180);CHECK_TRUE(restored->instructions().back().node.id==3);
}
TEST_SUITE_END
