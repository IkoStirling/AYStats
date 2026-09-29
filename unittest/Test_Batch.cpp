#include <AYTest.h>
#include <AYStats/Batch.h>
#include <AYTask/TaskScheduler.h>
#include "Fixtures.h"
#include <array>

namespace {
BatchInput bucket(std::size_t count) {
    BatchInput input;input.plan=std::make_shared<const CalculationPlan>(damagePlan());input.columns.resize(2);
    for(std::size_t i=0;i<count;++i){input.columns[0].push_back(Value(i+1));input.columns[1].push_back(15000);input.requests.push_back(i+1);input.contexts.push_back({.calculation=i+1});}return input;
}
}
TEST_SUITE(StatsBatchTests)
TEST_CASE(owned_parallel_results_match_scalar_and_block_sizes) {
    const auto input=bucket(10003);auto expected=BatchExecutor::scalar({input}).value();
    for(auto chunk:{std::size_t(127),std::size_t(4096)}) {
        ayt::task::TaskScheduler scheduler(3);auto job=BatchExecutor::submit({input},scheduler,{.blockSize=chunk});CHECK_TRUE(job.has_value());
        auto result=job->finish();CHECK_TRUE(result.has_value());CHECK_TRUE((*result)[0].values==expected[0].values);CHECK_TRUE((*result)[0].status==expected[0].status);
        CHECK_TRUE((*result)[0].simdLanes>9000);CHECK_TRUE(expected[0].simdLanes==0);
        CHECK_TRUE(job->ready());CHECK_FALSE(job->finish().has_value());scheduler.shutdown();
    }
}
TEST_CASE(validation_limits_and_sparse_errors) {
    auto input=bucket(3);input.requests[2]=input.requests[0];CHECK_TRUE(BatchExecutor::scalar({input}).error().code==Error::Duplicate);
    input=bucket(3);CHECK_TRUE(BatchExecutor::scalar({input},{.memoryBudget=1}).error().code==Error::Limit);
    input.columns[0][1]=std::numeric_limits<Value>::max();auto result=BatchExecutor::scalar({input}).value();
    CHECK_TRUE(result[0].failed==1);CHECK_TRUE(result[0].status[1]==2);CHECK_TRUE(result[0].errors[0].first==1);CHECK_TRUE(result[0].status[0]==1);
}
TEST_CASE(ordered_reads_and_snapshot_reads_are_distinct) {
    auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value());
    SettlementRequest first{.id=1,.plan=plan,.inputs={{.literal=80}},.writes={{0,statId("health"),MutationOp::Decrease}}};
    SettlementRequest second{.id=2,.plan=plan,.inputs={{0,statId("health"),ReadField::Current}},.writes={{0,statId("health"),MutationOp::Decrease}}};
    const std::array requests{first,second};
    auto a=fixture(),b=fixture();std::array<StatSet*,1> bankA{&a},bankB{&b};std::array<std::uint64_t,1> versions{a.version()};
    auto ordered=settleBatch(bankA,requests,BatchSemantics::Ordered,versions);auto snapshot=settleBatch(bankB,requests,BatchSemantics::Snapshot,versions);
    CHECK_TRUE(ordered->values[1]==20);CHECK_TRUE(snapshot->values[1]==100);CHECK_TRUE(snapshot->results[1][0].changes[0].actual==-20);
}
TEST_CASE(cross_state_failure_is_atomic) {
    auto a=fixture(),b=fixture();std::array<StatSet*,2> bank{&a,&b};const std::array versions{a.version(),b.version()};
    auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Constant,.value=80}},1,0).value());
    const std::array requests{SettlementRequest{.id=1,.plan=plan,.writes={{0,statId("health"),MutationOp::Decrease},{1,statId("missing"),MutationOp::Decrease}}}};
    CHECK_FALSE(settleBatch(bank,requests,BatchSemantics::Ordered,versions).has_value());CHECK_TRUE(a.read(statId("health"))->current==100);CHECK_TRUE(a.version()==versions[0]);
}
TEST_SUITE_END
