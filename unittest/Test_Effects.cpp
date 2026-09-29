#include <AYTest.h>
#include "Fixtures.h"
TEST_SUITE(StatsEffectTests)
TEST_CASE(expiration_and_refresh_tick_boundaries) {
    auto set=fixture();EffectManager manager;Effect effect{.id=1,.key=5,.expires=10,.modifiers={modifier(1,1,20)}};
    CHECK_TRUE(manager.apply(set,effect,EffectPolicy::Independent,0).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==120);
    effect.id=2;effect.expires=20;CHECK_TRUE(manager.apply(set,effect,EffectPolicy::Refresh,9).has_value());
    CHECK_TRUE(manager.advance(set,10).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==120);
    CHECK_TRUE(manager.advance(set,20).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==100);CHECK_TRUE(manager.advance(set,19).error().code==Error::Invalid);
}
TEST_CASE(independent_stacks_and_atomic_limits) {
    auto set=fixture();EffectManager manager;Effect a{.id=1,.key=1,.expires=5,.maxStacks=2,.modifiers={modifier(1,1,10)}};
    CHECK_TRUE(manager.apply(set,a,EffectPolicy::Stack,0).has_value());auto b=a;b.id=2;b.expires=8;b.modifiers[0].handle=2;
    CHECK_TRUE(manager.apply(set,b,EffectPolicy::Stack,0).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==120);
    b.id=3;b.modifiers[0].handle=3;const auto version=set.version();CHECK_TRUE(manager.apply(set,b,EffectPolicy::Stack,0).error().code==Error::Limit);CHECK_TRUE(set.version()==version);
    CHECK_TRUE(manager.advance(set,5).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==110);CHECK_TRUE(manager.remove(set,2).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==100);
}
TEST_CASE(reaction_causality_limits_and_denied_relations) {
    ReactionQueue queue(2,2);CalculationContext root{.tick=4,.calculation=1};CHECK_TRUE(queue.begin().has_value());
    CHECK_TRUE(queue.derive(root,{.calculation=2},true).has_value());CHECK_TRUE(queue.begin().error().code==Error::Reentrant);
    const auto child=queue.pop().value();CHECK_TRUE(child.parent==1 && child.depth==1 && child.tick==4);
    CHECK_TRUE(queue.derive(child,{.calculation=3},true).has_value());const auto grandchild=queue.pop().value();
    CHECK_TRUE(queue.derive(grandchild,{.calculation=4},true).error().code==Error::Limit);
    CHECK_TRUE(queue.begin().has_value());CHECK_TRUE(queue.derive(root,{.calculation=2},false).error().code==Error::Invalid);CHECK_TRUE(queue.pending()==0);
}
TEST_CASE(refresh_existing_identity_and_stack_policy_mismatch) {
    auto set=fixture();EffectManager manager;Effect effect{.id=1,.key=1,.expires=10,.maxStacks=2,.modifiers={modifier(1,1,20)}};
    CHECK_TRUE(manager.apply(set,effect,EffectPolicy::Stack,0).has_value());effect.expires=20;CHECK_TRUE(manager.apply(set,effect,EffectPolicy::Refresh,5).has_value());CHECK_TRUE(manager.effects().size()==1);
    CHECK_TRUE(manager.advance(set,10).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==120);
    effect.id=2;effect.modifiers[0].handle=2;effect.maxStacks=3;CHECK_TRUE(manager.apply(set,effect,EffectPolicy::Stack,10).error().code==Error::Invalid);CHECK_TRUE(manager.effects().size()==1);
}
TEST_SUITE_END
