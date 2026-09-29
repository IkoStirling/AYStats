#include <AYTest.h>
#include <AYStats/Batch.h>
#include <AYTask/TaskScheduler.h>
using namespace ayt::stats;
TEST_SUITE(StatsStressTests)
TEST_CASE(heterogeneous_random_table_and_complex_plans_match_across_workers) {
    std::vector<BatchInput> buckets;
    for(std::uint32_t recipe=0;recipe<32;++recipe) {
        std::vector<Node> nodes{{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=Value(10000+recipe*37)},{.id=3,.op=Op::MultiplyRate,.a=1,.b=2},{.id=4,.op=Op::Random,.binding=recipe+1,.value=17},{.id=5,.op=Op::Add,.a=3,.b=4},{.id=6,.op=Op::Input,.binding=1},{.id=7,.op=Op::Table,.a=6,.table={0,1,2,4,8,16,32,64}},{.id=8,.op=Op::Add,.a=5,.b=7}};
        BatchInput input;input.plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile(std::move(nodes),8,2).value());input.columns.resize(2);
        for(std::uint32_t lane=0;lane<2051;++lane){const auto id=std::uint64_t(recipe)*2051+lane+1;input.requests.push_back(id);input.contexts.push_back({.seed=947,.tick=12,.calculation=id,.hit=lane});input.columns[0].push_back(lane%10000);input.columns[1].push_back(lane%8);}
        buckets.push_back(std::move(input));
    }
    auto expected=BatchExecutor::scalar(buckets).value();
    for(auto workers:{1,4})for(auto block:{std::size_t(31),std::size_t(4096)}) {
        ayt::task::TaskScheduler scheduler(workers);auto job=BatchExecutor::submit(buckets,scheduler,{.blockSize=block});CHECK_TRUE(job.has_value());auto result=job->finish();CHECK_TRUE(result.has_value());
        for(std::size_t i=0;i<expected.size();++i){CHECK_TRUE((*result)[i].values==expected[i].values);CHECK_TRUE((*result)[i].status==expected[i].status);}scheduler.shutdown();
    }
}
TEST_SUITE_END
