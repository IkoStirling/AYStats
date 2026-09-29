#include <AYTest.h>
#include <AYTestFixtures.h>
#include <AYStats/AssetIO.h>
#include "Fixtures.h"
#include <array>
TEST_SUITE(StatsAssetTests)
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
