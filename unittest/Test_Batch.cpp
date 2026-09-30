#include <AYTest.h>
#include <AYStats/Batch.h>
#include <AYTask/TaskScheduler.h>
#include "Fixtures.h"
#include <array>
#include <thread>
#include <chrono>
#include <random>

namespace {
BatchInput bucket(std::size_t count) {
    BatchInput input;input.plan=std::make_shared<const CalculationPlan>(damagePlan());input.columns.resize(2);
    for(std::size_t i=0;i<count;++i){input.columns[0].push_back(Value(i+1));input.columns[1].push_back(15000);input.requests.push_back(i+1);input.contexts.push_back({.calculation=i+1});}return input;
}
}
TEST_SUITE(StatsBatchTests)
TEST_CASE(named_recipe_bank_is_owned_declared_and_parallel_matches_reference) {
    Recipe r{"attack-cost",statId("health"),AttributeOp::Decrease,damagePlan(),
        {{InputSource::SourceStat,statId("attack")},{InputSource::Amount}},true};
    auto book=RecipeBook::compile({r}).value();
    std::vector<StatSet> reference(131,fixture()),parallel=reference;
    std::vector<StatSet*> bank,referenceBank;std::vector<std::uint64_t> versions;
    for(std::size_t i=0;i<parallel.size();++i){bank.push_back(&parallel[i]);referenceBank.push_back(&reference[i]);versions.push_back(parallel[i].version());}
    std::vector<SettlementRequest> requests;
    for(std::size_t i=1;i<parallel.size();++i)
        requests.push_back(prepareRecipeRequest(book,{i,i,statId("health"),AttributeOp::Decrease,5000,"",0}).value());
    book.reset(); // Every request aliases the retained immutable book.
    CHECK_TRUE(requests[0].inputs[0].state==0 && requests[0].inputs[1].state==SIZE_MAX);
    const auto expected=settleBatch(referenceBank,requests,BatchSemantics::Ordered,versions).value();
    ayt::task::TaskScheduler scheduler(4);
    const auto actual=settleBatch(bank,requests,BatchSemantics::Ordered,versions,&scheduler).value();scheduler.shutdown();
    CHECK_TRUE(actual.conflictGroups==130 && actual.parallelTasks>=2);CHECK_TRUE(actual.values==expected.values);
    for(std::size_t i=1;i<parallel.size();++i){CHECK_TRUE(parallel[i].read(statId("health"))->current==50);
        CHECK_TRUE(actual.results[i-1][0].changes[0].actual==expected.results[i-1][0].changes[0].actual);}
}
TEST_CASE(recipe_bank_field_validation_and_scalar_overflow_are_atomic) {
    auto state=fixture();const std::array<StatSet*,1> bank{&state};const std::array versions{state.version()};
    Recipe r{"bad-pool",statId("health"),AttributeOp::Decrease,
        CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value(),{{InputSource::TargetStat,statId("attack"),InputField::Current}}};
    const auto book=RecipeBook::compile({r}).value();
    const std::array bad{prepareRecipeRequest(book,{1,0,statId("health"),AttributeOp::Decrease,0,"bad-pool"}).value()};
    CHECK_TRUE(settleBatch(bank,bad,BatchSemantics::Ordered,versions).error().code==Error::Invalid);
    CHECK_TRUE(state.version()==versions[0]);
    const std::array overflow{prepareRecipeRequest(nullptr,{1,0,statId("attack"),AttributeOp::Increase,INT64_MAX}).value()};
    CHECK_TRUE(settleBatch(bank,overflow,BatchSemantics::Ordered,versions).error().code==Error::Overflow);
    CHECK_TRUE(state.read(statId("attack"))->base==100 && state.version()==versions[0]);
}
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
TEST_CASE(later_writer_connects_readers_and_read_only_requests) {
    std::vector<StatSet> states(4,fixture());std::vector<StatSet*> bank;std::vector<std::uint64_t> versions;
    for(auto& state:states){bank.push_back(&state);versions.push_back(state.version());}
    const auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value());
    const std::vector<SettlementRequest> requests{
        {.id=1,.plan=plan,.inputs={{0,statId("health"),ReadField::Current}},.writes={{1,statId("health"),MutationOp::Decrease}}},
        {.id=2,.plan=plan,.inputs={{0,statId("health"),ReadField::Current}}},
        {.id=3,.plan=plan,.inputs={{.literal=80}},.writes={{0,statId("health"),MutationOp::Decrease}}},
        {.id=4,.plan=plan,.inputs={{0,statId("health"),ReadField::Current}},.writes={{2,statId("health"),MutationOp::Decrease}}},
        {.id=5,.plan=plan,.inputs={{.literal=10}},.writes={{3,statId("health"),MutationOp::Decrease}}}
    };
    ayt::task::TaskScheduler scheduler(3);SettlementMetrics metrics;
    const auto result=settleBatch(bank,requests,BatchSemantics::Ordered,versions,&scheduler,{},&metrics).value();scheduler.shutdown();
    CHECK_TRUE(result.conflictGroups==2);CHECK_TRUE(result.values==std::vector<Value>({100,100,80,20,10}));
    CHECK_TRUE(result.results[1].empty());CHECK_TRUE(states[2].read(statId("health"))->current==80);
    CHECK_TRUE(metrics.copiedStates==4 && metrics.evaluatedRequests==5 && metrics.committedWrites==4);
}
TEST_CASE(randomized_multi_write_settlement_matches_public_transaction_reference) {
    // Independent reference uses public atomic commit, with no batch grouping or shadow fast path.
    const auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value());
    Definition base{.name="base",.mode=Mode::Advanced,.initial=100,.maximum=1000,.zones={{1,ZoneOp::Flat}}};
    Definition derived{.name="derived",.mode=Mode::Advanced,.maximum=1000,.baseFrom=statId("base")};
    for(const auto policy:{CapacityPolicy::KeepCurrent,CapacityPolicy::KeepRatio,CapacityPolicy::KeepMissing})for(const auto semantics:{BatchSemantics::Snapshot,BatchSemantics::Ordered}) {
        const auto schema=Schema::compile({base,derived,
            {.name="resource",.shape=Shape::Pool,.mode=Mode::Advanced,.current=75,.capacity=100,.maximum=1000,.capacityPolicy=policy,.capacityFrom=statId("derived")},
            {.name="direct",.shape=Shape::Pool,.current=75,.capacity=100,.maximum=1000,.capacityPolicy=policy}}).value();
        auto initial=StatSet::create(schema).value();CHECK_TRUE(initial.insert({.handle=1,.effect=1,.rule=1,.target=statId("base"),.zone=1,.value=5}).has_value());
        std::vector<StatSet> states(37,initial),reference=states,original=states;
        std::vector<StatSet*> bank;std::vector<std::uint64_t> versions;
        for(auto& state:states){bank.push_back(&state);versions.push_back(state.version());}
        std::mt19937_64 random(7313);std::vector<SettlementRequest> requests;
        for(std::size_t i=0;i<700;++i){const auto target=random()%states.size();
            SettlementRequest request{.id=i+1,.plan=plan,.inputs={{.literal=Value(random()%250)}}};
            switch(i%5){
            case 0:request.writes={{target,statId("base"),MutationOp::SetBase}};break;
            case 1:request.writes={{target,statId("direct"),MutationOp::SetCapacity}};break;
            default:request.inputs={{random()%states.size(),statId("resource"),ReadField::Current}};
                request.writes={{target,statId("resource"),MutationOp::Decrease},{(target+1)%states.size(),statId("direct"),MutationOp::Increase}};break;}
            requests.push_back(std::move(request));
        }
        BatchSettlement expected;
        for(const auto& request:requests){const auto& binding=request.inputs[0];Value value=binding.literal;
            if(binding.state!=SIZE_MAX)value=(semantics==BatchSemantics::Snapshot?original:reference)[binding.state].read(binding.stat)->current;
            expected.values.push_back(value);expected.results.emplace_back();
            for(const auto& target:request.writes){Mutation operation{target.stat,target.op,value};auto& state=reference[target.state];
                expected.results.back().push_back(state.commit(std::span(&operation,1),state.version()).value());}}
        ayt::task::TaskScheduler scheduler(4);SettlementMetrics metrics;
        const auto result=settleBatch(bank,requests,semantics,versions,&scheduler,{},&metrics).value();scheduler.shutdown();
        CHECK_TRUE(result.values==expected.values);CHECK_TRUE(metrics.copiedStates==states.size());
        for(std::size_t i=0;i<states.size();++i){CHECK_TRUE(states[i].version()==reference[i].version());
            for(std::size_t j=0;j<states[i].rows().size();++j){const auto a=states[i].rows()[j],b=reference[i].rows()[j];
                CHECK_TRUE(a.base==b.base && a.current==b.current && a.capacity==b.capacity && a.effective==b.effective);}}
        for(std::size_t r=0;r<requests.size();++r)for(std::size_t w=0;w<requests[r].writes.size();++w){const auto& a=result.results[r][w];const auto& b=expected.results[r][w];
            CHECK_TRUE(a.beforeVersion==b.beforeVersion && a.afterVersion==b.afterVersion && a.changes.size()==b.changes.size());
            for(std::size_t c=0;c<a.changes.size();++c){const auto x=a.changes[c],y=b.changes[c];
                CHECK_TRUE(x.target==y.target && x.before==y.before && x.after==y.after && x.actual==y.actual && x.excess==y.excess && x.field==y.field);}}
    }
}
TEST_CASE(late_scalar_rebuild_failure_discards_all_in_place_shadows) {
    Definition base{.name="base",.mode=Mode::Advanced,.initial=1};
    Definition derived{.name="derived",.mode=Mode::Advanced,.baseFrom=statId("base"),.derivedFactor=20000};
    const auto schema=Schema::compile({base,derived,{.name="resource",.shape=Shape::Pool,.current=100,.capacity=100}}).value();
    std::vector<StatSet> states(130,StatSet::create(schema).value());std::vector<StatSet*> bank;std::vector<std::uint64_t> versions;
    for(auto& state:states){bank.push_back(&state);versions.push_back(state.version());}
    const auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value());
    std::vector<SettlementRequest> requests;
    for(std::size_t i=0;i<states.size();++i)requests.push_back({.id=i+1,.plan=plan,.inputs={{.literal=10}},.writes={{i,statId("resource"),MutationOp::Decrease}}});
    requests.push_back({.id=131,.plan=plan,.inputs={{.literal=INT64_MAX}},.writes={{129,statId("base"),MutationOp::SetBase}}});
    ayt::task::TaskScheduler scheduler(3);auto result=settleBatch(bank,requests,BatchSemantics::Ordered,versions,&scheduler);scheduler.shutdown();
    CHECK_TRUE(!result && result.error().code==Error::Overflow);
    for(const auto& state:states){CHECK_TRUE(state.version()==1 && state.read(statId("base"))->base==1 && state.read(statId("resource"))->current==100);}
}
TEST_CASE(scheduler_rejection_and_exception_drain_accepted_work_without_publication) {
    class Refusing final:public ayt::task::ITaskScheduler {
    public:
        ayt::task::TaskScheduler owned{2};bool throws=false;std::size_t submitted=0,waited=0;
        bool submitOwned(std::shared_ptr<ayt::task::ITask> task) override {
            if(submitted==1){if(throws)throw std::runtime_error("submission rejected");return false;}
            ++submitted;return owned.submitOwned(std::move(task));
        }
        void submit(ayt::task::ITask* t) override {owned.submit(t);}
        void submit(std::initializer_list<ayt::task::ITask*> t) override {owned.submit(t);}
        void wait(ayt::task::ITask* t) override {++waited;owned.wait(t);}
        void waitAll() override {owned.waitAll();}
        void shutdown() override {owned.shutdown();}
        bool isShuttingDown() const override {return owned.isShuttingDown();}
        std::size_t numThreads() const override {return owned.numThreads();}
        std::size_t numQueuedTasks() const override {return owned.numQueuedTasks();}
        std::size_t numRunningTasks() const override {return owned.numRunningTasks();}
    };
    const auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Constant,.value=10}},1,0).value());
    for(const bool throws:{false,true}){std::vector<StatSet> states(130,fixture());std::vector<StatSet*> bank;std::vector<std::uint64_t> versions;
        std::vector<SettlementRequest> requests;
        for(std::size_t i=0;i<states.size();++i){bank.push_back(&states[i]);versions.push_back(states[i].version());requests.push_back({.id=i+1,.plan=plan,.writes={{i,statId("health"),MutationOp::Decrease}}});}
        Refusing scheduler;scheduler.throws=throws;
        const auto result=settleBatch(bank,requests,BatchSemantics::Ordered,versions,&scheduler);
        CHECK_TRUE(!result && result.error().code==Error::Unsupported);CHECK_TRUE(scheduler.submitted==1 && scheduler.waited==1);
        for(const auto& state:states)CHECK_TRUE(state.version()==1 && state.read(statId("health"))->current==100);
        scheduler.shutdown();}
}
TEST_SUITE_END
