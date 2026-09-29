#include <AYStats/Batch.h>
#include <AYTask/TaskScheduler.h>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
using namespace ayt::stats;
namespace {
BatchInput input(std::shared_ptr<const CalculationPlan> plan,std::size_t n) {
    BatchInput b;b.plan=std::move(plan);b.columns.resize(2);for(auto& c:b.columns)c.resize(n);b.requests.resize(n);b.contexts.resize(n);
    for(std::size_t i=0;i<n;++i){b.columns[0][i]=Value(i%10000+1);b.columns[1][i]=15000;b.requests[i]=i+1;b.contexts[i].calculation=i+1;}return b;
}
}
int main(int argc,char** argv) {
    try {
        const auto n=argc>1?std::stoull(argv[1]):100000ull;
        if(!n || n>1000000)throw std::runtime_error("count must be 1..1000000");
        auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input,.binding=0},{.id=2,.op=Op::Input,.binding=1},{.id=3,.op=Op::MultiplyRate,.a=1,.b=2}},3,2).value());
        std::vector<Value> reference;std::cout<<"{\"count\":"<<n<<",\"runs\":[";
        for(int mode=0;mode<3;++mode) {
            ayt::task::TaskScheduler scheduler(mode==2?4:1);
            std::vector<double> times;std::size_t simd=0;
            for(int round=0;round<6;++round) {
                const auto start=std::chrono::steady_clock::now();auto data=input(plan,std::size_t(n));
                Result<std::vector<BatchOutput>> output=std::unexpected(Diagnostic{});
                std::vector<BatchInput> owned;owned.push_back(std::move(data));
                if(mode==0)output=BatchExecutor::scalar(std::move(owned));
                else {auto job=BatchExecutor::submit(std::move(owned),scheduler);if(!job)throw std::runtime_error(job.error().message);output=job->finish();}
                const auto end=std::chrono::steady_clock::now();if(!output)throw std::runtime_error(output.error().message);
                if((*output)[0].failed || (*output)[0].cancelled)throw std::runtime_error("unexpected failed lanes");
                if(mode==0 && round==0)reference=(*output)[0].values;else if(reference!=(*output)[0].values)throw std::runtime_error("CPU backend mismatch");
                simd=(*output)[0].simdLanes;if(round)times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
            }
            scheduler.shutdown();std::sort(times.begin(),times.end());
            if(mode)std::cout<<",";std::cout<<"{\"backend\":\""<<(mode==0?"scalar":mode==1?"simd-1-worker":"simd-4-workers")<<"\",\"median_ms\":"<<times[times.size()/2]<<",\"max_ms\":"<<times.back()<<",\"simd_lanes\":"<<simd<<"}";
        }
        std::cout<<"],\"scope\":\"input construction+validation+dispatch+evaluate+finish; excludes plan compile/scheduler startup/settlement\"}\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
