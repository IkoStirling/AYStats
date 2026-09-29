#include <AYTest.h>
#include <AYStats/Batch.h>
#include "Fixtures.h"
#include <array>
TEST_SUITE(StatsExampleTests)
TEST_CASE(advanced_damage_shield_actual_health_and_actual_leech) {
    auto source=fixture();CHECK_TRUE(source.insert(modifier(1,1,20)).has_value());CHECK_TRUE(source.insert(modifier(2,2,5000)).has_value());CHECK_TRUE(source.insert(modifier(3,3,11000)).has_value());
    CHECK_TRUE(source.commit(std::array{Mutation{statId("health"),MutationOp::SetCurrent,40}},source.version()).has_value());
    auto target=StatSet::create(Schema::compile({{.name="health",.shape=Shape::Pool,.current=50,.capacity=100},{.name="shield",.shape=Shape::Pool,.current=30,.capacity=30}}).value()).value();
    std::vector<Node> nodes{{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=5000},{.id=3,.op=Op::MultiplyRate,.a=1,.b=2},{.id=4,.op=Op::Input,.binding=1},{.id=5,.op=Op::Minimum,.a=3,.b=4},{.id=6,.op=Op::Subtract,.a=3,.b=5}};
    auto shield=std::make_shared<const CalculationPlan>(CalculationPlan::compile(nodes,5,2).value());auto health=std::make_shared<const CalculationPlan>(CalculationPlan::compile(nodes,6,2).value());
    CHECK_TRUE(trace(*health,std::array<Value,2>{198,30},{.calculation=1})->result==69);CHECK_TRUE(target.read(statId("health"))->current==50);
    const std::vector<Binding> bindings{{0,statId("attack")},{1,statId("shield"),ReadField::Current}};
    const std::array requests{SettlementRequest{.id=1,.plan=shield,.inputs=bindings,.writes={{1,statId("shield"),MutationOp::Decrease}}},SettlementRequest{.id=2,.plan=health,.inputs=bindings,.writes={{1,statId("health"),MutationOp::Decrease}}}};
    const std::array<StatSet*,2> bank{&source,&target};const std::array versions{source.version(),target.version()};auto settled=settleBatch(bank,requests,BatchSemantics::Snapshot,versions);CHECK_TRUE(settled.has_value());
    CHECK_TRUE(settled->values[0]==30);CHECK_TRUE(settled->values[1]==69);const auto actual=-settled->results[1][0].changes[0].actual;CHECK_TRUE(actual==50);CHECK_TRUE(settled->results[1][0].changes[0].excess==19);
    ReactionQueue reactions;CHECK_TRUE(reactions.begin().has_value());CHECK_TRUE(reactions.derive({.calculation=1},{.calculation=3},true).has_value());CHECK_TRUE(reactions.pop()->parent==1);
    const auto healing=multiplyDivide(actual,2000,RateScale).value();CHECK_TRUE(healing==10);CHECK_TRUE(source.commit(std::array{Mutation{statId("health"),MutationOp::Increase,healing}},source.version()).has_value());
    CHECK_TRUE(source.read(statId("health"))->current==50);CHECK_TRUE(target.read(statId("health"))->current==0);CHECK_TRUE(target.read(statId("shield"))->current==0);
}
TEST_SUITE_END
