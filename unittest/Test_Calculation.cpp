#include <AYTest.h>
#include "Fixtures.h"
#include <array>
TEST_SUITE(StatsCalculationTests)
TEST_CASE(pure_intermediate_values_and_trace) {
    auto plan=damagePlan();const std::array<Value,2> inputs{120,15000};std::array<Value,3> scratch{};
    CHECK_TRUE(plan.evaluate(inputs,{},scratch).value()==180);CHECK_TRUE(inputs[0]==120);
    auto traced=trace(plan,inputs,{});CHECK_TRUE(traced->result==180);CHECK_TRUE(traced->stages.size()==3);CHECK_TRUE(trace(plan,inputs,{},2).error().code==Error::Limit);
}
TEST_CASE(forward_references_cycles_and_missing_nodes) {
    auto plan=CalculationPlan::compile({{.id=3,.op=Op::Add,.a=1,.b=2},{.id=1,.op=Op::Constant,.value=10},{.id=2,.op=Op::Constant,.value=20}},3,0);
    CHECK_TRUE(trace(*plan,{},{})->result==30);
    auto cycle=CalculationPlan::compile({{.id=1,.op=Op::Add,.a=2,.b=2},{.id=2,.op=Op::Add,.a=1,.b=1}},1,0);
    CHECK_TRUE(cycle.error().code==Error::Cycle);CHECK_FALSE(cycle.error().path.empty());
    CHECK_TRUE(CalculationPlan::compile({{.id=1,.op=Op::Add,.a=2,.b=2}},1,0).error().code==Error::Missing);
}
TEST_CASE(errors_have_stage_identity) {
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Constant,.value=1},{.id=2,.op=Op::Constant,.value=0},{.id=3,.op=Op::Divide,.a=1,.b=2}},3,0).value();
    std::array<Value,3> scratch{};auto result=plan.evaluate({}, {},scratch);CHECK_TRUE(result.error().code==Error::DivideByZero);CHECK_TRUE(result.error().path=="3");
}
TEST_CASE(random_is_schedule_independent) {
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Random,.binding=42,.value=1000}},1,0).value();std::array<Value,1> scratch{};
    CalculationContext c{.seed=123,.tick=5,.calculation=7,.hit=8};const auto first=plan.evaluate({},c,scratch).value();c.hit=9;CHECK_TRUE(plan.evaluate({},c,scratch).value()<1000);
    c.hit=8;CHECK_TRUE(plan.evaluate({},c,scratch).value()==first);CHECK_TRUE(first>=0);CHECK_TRUE(randomBits(c,42)!=randomBits(c,43));
}
TEST_CASE(explicit_extensions_and_exception_diagnostics) {
    std::vector<Node> nodes{{.id=1,.op=Op::Constant,.value=4},{.id=2,.op=Op::Extension,.a=1,.b=1,.extension="square"}};
    CHECK_TRUE(CalculationPlan::compile(nodes,2,0).error().code==Error::Missing);
    auto plan=CalculationPlan::compile(nodes,2,0,{{"square",[](auto values,const auto&)->Result<Value>{return multiplyDivide(values[0],values[1],1);}}}).value();
    CHECK_TRUE(trace(plan,{},{})->result==16);CHECK_TRUE(plan.hasExtensions());
    auto broken=CalculationPlan::compile(nodes,2,0,{{"square",[](auto,const auto&)->Result<Value>{throw std::runtime_error("bad");}}}).value();CHECK_TRUE(trace(broken,{},{}).error().path=="2");
}
TEST_SUITE_END
