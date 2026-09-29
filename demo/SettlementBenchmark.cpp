#include <AYStats/Batch.h>
#include <AYTask/TaskScheduler.h>
#include <algorithm>
#include <chrono>
#include <iostream>
using namespace ayt::stats;
int main(){
    auto schema=Schema::compile({{.name="resource",.shape=Shape::Pool,.current=100,.capacity=100}}).value();
    auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=1},{.id=3,.op=Op::Minimum,.a=1,.b=2}},3,1).value());
    ayt::task::TaskScheduler scheduler(4);std::cout<<"requests,distribution,semantics,median_ms,max_ms,conflict_groups,tasks\n";
    for(auto n:{1,1000,10000,100000,1000000})for(int distribution=0;distribution<3;++distribution)for(auto semantics:{BatchSemantics::Snapshot,BatchSemantics::Ordered}) {
        std::vector<double> samples;std::size_t groups=0,tasks=0;
        for(int run=0;run<4;++run) {
            const auto start=std::chrono::steady_clock::now();const auto targets=distribution==0?1:std::min(n,1000);std::vector<StatSet> states;states.reserve(targets);
            std::vector<StatSet*> bank;std::vector<std::uint64_t> versions;for(int i=0;i<targets;++i)states.push_back(StatSet::create(schema).value());for(auto& state:states){bank.push_back(&state);versions.push_back(state.version());}
            std::vector<SettlementRequest> requests;requests.reserve(n);
            for(int i=0;i<n;++i){const auto target=std::size_t(i%targets);const auto source=distribution==2?(target+1)%targets:target;requests.push_back({.id=std::uint64_t(i+1),.plan=plan,.inputs={{source,statId("resource"),ReadField::Current}},.writes={{target,statId("resource"),MutationOp::Decrease}}});}
            auto result=settleBatch(bank,requests,semantics,versions,&scheduler);if(!result){std::cerr<<result.error().message;return 1;}groups=result->conflictGroups;tasks=result->parallelTasks;
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(run)samples.push_back(elapsed);
        }
        std::sort(samples.begin(),samples.end());std::cout<<n<<','<<(distribution==0?"hotspot":distribution==1?"distributed":"cross-dependent")<<','<<(semantics==BatchSemantics::Ordered?"ordered":"snapshot")<<','<<samples[1]<<','<<samples.back()<<','<<groups<<','<<tasks<<'\n';
    }
    scheduler.shutdown();return 0;
}
