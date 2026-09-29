#include <AYTest.h>
#include <AYStats/Batch.h>
#include <AYTask/TaskScheduler.h>
#include "Fixtures.h"
#include <array>
#include <thread>
#include <chrono>

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
TEST_CASE(shared_read_only_source_does_not_serialize_independent_targets) {
    std::vector<StatSet> states;for(int i=0;i<131;++i)states.push_back(fixture());
    std::vector<StatSet*> bank;std::vector<std::uint64_t> versions;for(auto& state:states){bank.push_back(&state);versions.push_back(state.version());}
    auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value());
    std::vector<SettlementRequest> requests;
    for(std::size_t i=1;i<states.size();++i)requests.push_back({.id=i,.plan=plan,.inputs={{0,statId("attack")}},.writes={{i,statId("health"),MutationOp::Decrease}}});
    ayt::task::TaskScheduler scheduler(3);auto result=settleBatch(bank,requests,BatchSemantics::Ordered,versions,&scheduler);
    CHECK_TRUE(result.has_value());CHECK_TRUE(result->conflictGroups==130);CHECK_TRUE(result->parallelTasks==3);
    for(std::size_t i=1;i<states.size();++i)CHECK_TRUE(states[i].read(statId("health"))->current==0);
    scheduler.shutdown();
}
TEST_CASE(cancellation_preserves_owned_results_and_completes_running_blocks) {
    std::atomic_bool release=false,entered=false;
    Extensions extensions{{"test.gate",[&](std::span<const Value> values,const CalculationContext&)->Result<Value>{entered.store(true);while(!release.load())std::this_thread::yield();return values[0];}}};
    auto input=bucket(1024);input.plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant},{.id=3,.op=Op::Extension,.a=1,.b=2,.extension="test.gate"}},3,2,std::move(extensions)).value());
    ayt::task::TaskScheduler scheduler(2);auto job=BatchExecutor::submit({std::move(input)},scheduler,{.blockSize=32});CHECK_TRUE(job.has_value());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(!entered.load() && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    CHECK_TRUE(entered.load());job->cancel();release.store(true);auto output=job->finish();CHECK_TRUE(output.has_value());CHECK_TRUE((*output)[0].cancelled>0);CHECK_TRUE((*output)[0].failed==0);
    CHECK_TRUE(job->ready());for(auto status:(*output)[0].status)CHECK_TRUE(status==1 || status==3);scheduler.shutdown();
}
TEST_CASE(settlement_budget_refuses_before_any_live_write) {
    auto state=fixture();const std::array<StatSet*,1> bank{&state};const std::array versions{state.version()};
    CHECK_TRUE(settleBatch(bank,{},BatchSemantics::Snapshot,versions,nullptr,{.memoryBudget=1}).error().code==Error::Limit);CHECK_TRUE(state.version()==versions[0]);
}
TEST_SUITE_END
