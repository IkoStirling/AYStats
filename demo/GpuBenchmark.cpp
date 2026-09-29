#include <AYStats/Gpu.h>
#include <AYTask/TaskScheduler.h>
#include <algorithm>
#include <chrono>
#include <iostream>
using namespace ayt::stats;
int main(int argc,char**) {
    auto device=GpuExecutor::create();if(!device){std::cerr<<device.error().message;return 1;}
    if(argc>1){auto info=(*device)->deviceInfo();std::cout<<"device="<<info.name<<" vendor="<<info.vendor<<" id="<<info.device<<" dedicated_bytes="<<info.dedicatedBytes<<'\n';return 0;}
    auto simple=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Input,.binding=1},{.id=3,.op=Op::MultiplyRate,.a=1,.b=2}},3,2).value());
    std::vector<Node> chain{{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=10001}};
    for(std::uint32_t id=3;id<51;++id)chain.push_back({.id=id,.op=Op::MultiplyRate,.a=id==3?1:id-1,.b=2});
    auto complex=std::make_shared<const CalculationPlan>(CalculationPlan::compile(std::move(chain),50,2).value());
    ayt::task::TaskScheduler scheduler(4);
    std::cout<<"count,recipe,backend,median_ms,max_ms\n";
    for(auto plan:{simple,complex})for(auto n:{1,1000,10000,100000,1000000}) {
    std::vector<Value> reference;
    for(auto backend:{BackendChoice::Cpu,BackendChoice::Gpu}) {
        std::vector<double> samples;
        for(int run=0;run<6;++run) {
            const auto start=std::chrono::steady_clock::now();BatchInput input;input.plan=plan;input.columns.resize(2);
            input.requests.reserve(n);input.contexts.reserve(n);for(auto& c:input.columns)c.reserve(n);
            for(int i=0;i<n;++i){input.requests.push_back(i+1);input.contexts.push_back({.calculation=std::uint64_t(i+1)});input.columns[0].push_back(i%10000);input.columns[1].push_back(15001);}
            auto result=(*device)->execute(std::move(input),backend,{},&scheduler);if(!result){std::cerr<<result.error().message;return 2;}
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(backend==BackendChoice::Cpu && run==0)reference=result->output.values;
            else if(reference!=result->output.values || result->output.failed || result->output.cancelled){std::cerr<<"backend result mismatch";return 3;}
            if(run)samples.push_back(elapsed);
        }
        std::sort(samples.begin(),samples.end());std::cout<<n<<','<<(plan==simple?"simple-rate":"48-stage-rate")<<','<<(backend==BackendChoice::Gpu?"gpu":"cpu-4")<<','<<samples[2]<<','<<samples.back()<<'\n';
    }
    }
    scheduler.shutdown();return 0;
}
