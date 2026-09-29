#include <AYTest.h>
#include "Fixtures.h"
#include <array>
TEST_SUITE(StatsCoreTests)
TEST_CASE(integer_boundaries_and_widened_product) {
    const auto max=std::numeric_limits<Value>::max(),min=std::numeric_limits<Value>::min();
    CHECK_TRUE(add(max,1).error().code==Error::Overflow);CHECK_TRUE(add(min,-1).error().code==Error::Overflow);
    CHECK_TRUE(multiplyDivide(max,max,max).value()==max);CHECK_TRUE(multiplyDivide(min,1,1).value()==min);
    CHECK_TRUE(multiplyDivide(min,-1,1).error().code==Error::Overflow);CHECK_TRUE(multiplyDivide(1,1,0).error().code==Error::DivideByZero);
    CHECK_TRUE(multiplyDivide(-7,2,3).value()==-4);CHECK_TRUE(multiplyDivide(max,2,2).value()==max);
}
TEST_CASE(actual_attribution_and_atomic_failure) {
    auto set=fixture();const auto version=set.version();
    const std::array bad{Mutation{statId("health"),MutationOp::Decrease,30},Mutation{statId("missing"),MutationOp::Decrease,10}};
    CHECK_FALSE(set.commit(bad,version).has_value());CHECK_TRUE(set.version()==version);CHECK_TRUE(set.read(statId("health"))->current==100);
    const std::array ops{Mutation{statId("health"),MutationOp::Decrease,80},Mutation{statId("health"),MutationOp::Decrease,80}};
    auto result=set.commit(ops,version);CHECK_TRUE(result.has_value());CHECK_TRUE(result->changes[0].actual==-80);
    CHECK_TRUE(result->changes[1].actual==-20);CHECK_TRUE(result->changes[1].excess==60);
    CHECK_TRUE(set.commit(ops,version).error().code==Error::Stale);
    CHECK_TRUE(set.commit(std::array{Mutation{statId("health"),MutationOp::Decrease,-1}},set.version()).error().code==Error::Invalid);
}
TEST_CASE(capacity_policies_and_zero) {
    for(auto policy:{CapacityPolicy::KeepCurrent,CapacityPolicy::KeepRatio,CapacityPolicy::KeepMissing}) {
        auto schema=Schema::compile({{.name="pool",.shape=Shape::Pool,.current=40,.capacity=100,.capacityPolicy=policy}}).value();
        auto set=StatSet::create(schema).value();CHECK_TRUE(set.commit(std::array{Mutation{statId("pool"),MutationOp::SetCapacity,200}},set.version()).has_value());
        const auto expected=policy==CapacityPolicy::KeepCurrent?40:policy==CapacityPolicy::KeepRatio?80:140;
        CHECK_TRUE(set.read(statId("pool"))->current==expected);
        CHECK_TRUE(set.commit(std::array{Mutation{statId("pool"),MutationOp::SetCapacity,0}},set.version()).has_value());CHECK_TRUE(set.read(statId("pool"))->current==0);
    }
}
TEST_CASE(explicit_zones_removal_and_duplicate_identity) {
    auto set=fixture();CHECK_TRUE(set.insert(modifier(1,1,20)).has_value());CHECK_TRUE(set.insert(modifier(2,2,3000)).has_value());
    CHECK_TRUE(set.insert(modifier(3,2,2000)).has_value());CHECK_TRUE(set.insert(modifier(4,3,11000)).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==198);
    CHECK_TRUE(set.erase(4).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==180);
    auto m=modifier(2,2,3000);m.handle=5;CHECK_TRUE(set.insert(m).error().code==Error::Duplicate);
    m.rule=5;m.stage=5;CHECK_TRUE(set.insert(m).error().code==Error::Invalid);
    CHECK_TRUE(set.erase(1).has_value());CHECK_TRUE(set.read(statId("attack"))->effective==150);
}
TEST_CASE(stable_override_and_update) {
    auto a=fixture(),b=fixture();auto m=modifier(1,4,200),n=modifier(2,4,300);
    CHECK_TRUE(a.insert(m).has_value());CHECK_TRUE(a.insert(n).has_value());CHECK_TRUE(b.insert(n).has_value());CHECK_TRUE(b.insert(m).has_value());
    CHECK_TRUE(a.read(statId("attack"))->effective==b.read(statId("attack"))->effective);
    n.priority=1;CHECK_TRUE(a.replace(n).has_value());CHECK_TRUE(a.read(statId("attack"))->effective==300);
}
TEST_CASE(derived_capacity_and_cycle_path) {
    Definition cap{.name="max",.mode=Mode::Advanced,.initial=100,.zones={{1,ZoneOp::Flat}}};
    Definition hp{.name="hp",.shape=Shape::Pool,.mode=Mode::Advanced,.current=40,.capacity=100,.capacityPolicy=CapacityPolicy::KeepRatio,.capacityFrom=statId("max")};
    auto set=StatSet::create(Schema::compile({cap,hp}).value()).value();
    CHECK_TRUE(set.insert({.handle=1,.effect=1,.rule=1,.target=statId("max"),.zone=1,.value=100}).has_value());
    CHECK_TRUE(set.read(statId("hp"))->current==80);CHECK_TRUE(set.read(statId("hp"))->capacity==200);
    cap.baseFrom=statId("other");Definition other{.name="other",.mode=Mode::Advanced,.baseFrom=statId("max")};
    auto invalid=Schema::compile({cap,other});CHECK_TRUE(invalid.error().code==Error::Cycle);CHECK_TRUE(invalid.error().path.find("max")!=std::string::npos);
}
TEST_CASE(restore_rebuilds_and_rejects_bad_state) {
    auto set=fixture();CHECK_TRUE(set.insert(modifier(1,1,20)).has_value());auto rows=std::vector<Row>(set.rows().begin(),set.rows().end());
    for(auto& row:rows)row.effective=99999;
    auto restored=StatSet::restore(set.schema(),rows,set.modifiers(),set.version());CHECK_TRUE(restored.has_value());CHECK_TRUE(restored->read(statId("attack"))->effective==120);
    rows[*set.schema()->index(statId("health"))].current=-1;CHECK_FALSE(StatSet::restore(set.schema(),rows,set.modifiers(),set.version()).has_value());
}
TEST_SUITE_END
