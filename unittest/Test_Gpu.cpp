#include <AYTest.h>
#include <AYStats/Gpu.h>
using namespace ayt::stats;
namespace {
BatchInput gpuInput(std::size_t count) {
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Input,.binding=0},{.id=2,.op=Op::Input,.binding=1},{.id=3,.op=Op::MultiplyRate,.a=1,.b=2}},3,2).value();
    BatchInput input;input.plan=std::make_shared<const CalculationPlan>(std::move(plan));input.columns.resize(2);
    for(std::size_t i=0;i<count;++i){input.requests.push_back(i+1);input.contexts.push_back({.calculation=i+1});input.columns[0].push_back(Value(i%20001)-10000);input.columns[1].push_back(15001);}
    return input;
}
}
TEST_SUITE(StatsGpuTests)
TEST_CASE(hardware_integer_equivalence_and_non_aligned_tail) {
    auto executor=GpuExecutor::create();if(!executor)SKIP_TEST("hardware device unavailable: "+executor.error().message);
    for(auto count:{std::size_t(1),std::size_t(257),std::size_t(10003)}) {
        auto input=gpuInput(count);auto cpu=BatchExecutor::scalar({input}).value()[0];
        auto result=(*executor)->execute(input,BackendChoice::Gpu);CHECK_TRUE(result.has_value());if(!result)return;
        CHECK_TRUE(result->usedGpu);CHECK_TRUE(result->output.values==cpu.values);CHECK_TRUE(result->output.status==cpu.status);
    }
}
TEST_CASE(qualification_fallback_cancellation_and_device_lifetime) {
    auto executor=GpuExecutor::create();if(!executor)SKIP_TEST("hardware device unavailable: "+executor.error().message);
    auto input=gpuInput(257);auto automatic=(*executor)->execute(input);CHECK_TRUE(automatic.has_value());CHECK_FALSE(automatic->usedGpu);
    CHECK_TRUE((*executor)->execute(input,static_cast<BackendChoice>(999)).error().code==Error::Invalid);
    input.columns[0][0]=INT64_MAX/2;auto qualified=GpuExecutor::qualify(input);CHECK_TRUE(qualified.has_value());CHECK_FALSE(qualified->supported);
    CHECK_TRUE((*executor)->execute(input,BackendChoice::Gpu).error().code==Error::Unsupported);
    auto fallback=(*executor)->execute(input,BackendChoice::Automatic,1);CHECK_TRUE(fallback.has_value());CHECK_FALSE(fallback->usedGpu);
    auto job=(*executor)->submit(gpuInput(257));CHECK_TRUE(job.has_value());executor->reset();job->cancel();CHECK_TRUE(job->finish().error().code==Error::Cancelled);CHECK_FALSE(job->finish().has_value());
}
TEST_CASE(strict32_boundaries_and_interval_rejections) {
    auto input=gpuInput(3);input.plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=1},{.id=3,.op=Op::Divide,.a=1,.b=2}},3,2).value());
    input.columns[0]={INT32_MIN,0,INT32_MAX};auto executor=GpuExecutor::create();if(!executor)SKIP_TEST("hardware device unavailable: "+executor.error().message);
    auto result=(*executor)->execute(input,BackendChoice::Gpu);CHECK_TRUE(result.has_value());CHECK_TRUE(result->output.values==input.columns[0]);
    input.plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=-1},{.id=3,.op=Op::Divide,.a=1,.b=2}},3,2).value());
    CHECK_FALSE(GpuExecutor::qualify(input)->supported);
}
TEST_SUITE_END
