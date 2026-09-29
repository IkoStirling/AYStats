// Isolated instrumentation executable. Its allocation counters add overhead;
// use the other benchmark executables for the uninstrumented latency baseline.
#include <AYStats/Batch.h>
#include <AYTask/TaskScheduler.h>
#ifdef AY_STATS_METRICS_GPU
#include <AYStats/Gpu.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
namespace metrics {
std::atomic_size_t calls=0,live=0,peak=0;
struct alignas(std::max_align_t) Header {void* raw;std::size_t bytes;};
void* allocate(std::size_t bytes,std::size_t alignment) {
    alignment=std::max(alignment,alignof(Header));
    if(bytes>SIZE_MAX-alignment-sizeof(Header))throw std::bad_alloc();
    auto* raw=std::malloc(bytes+alignment+sizeof(Header));if(!raw)throw std::bad_alloc();
    const auto address=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(Header)+alignment-1)&~(alignment-1);
    auto* header=reinterpret_cast<Header*>(address)-1;header->raw=raw;header->bytes=bytes;
    calls.fetch_add(1);const auto total=live.fetch_add(bytes)+bytes;auto observed=peak.load();
    while(observed<total && !peak.compare_exchange_weak(observed,total)){}
    return reinterpret_cast<void*>(address);
}
void release(void* ptr) noexcept {if(!ptr)return;auto* header=static_cast<Header*>(ptr)-1;live.fetch_sub(header->bytes);std::free(header->raw);}
std::size_t reset(){calls=0;const auto initial=live.load();peak=initial;return initial;}
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
}
void* operator new(std::size_t n){return metrics::allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {metrics::release(p);}
void operator delete[](void* p) noexcept {metrics::release(p);}
void operator delete(void* p,std::size_t) noexcept {metrics::release(p);}
void operator delete[](void* p,std::size_t) noexcept {metrics::release(p);}
void* operator new(std::size_t n,std::align_val_t a){return metrics::allocate(n,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {metrics::release(p);}
void operator delete[](void* p,std::align_val_t) noexcept {metrics::release(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {metrics::release(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {metrics::release(p);}
using namespace ayt::stats;
class CountingScheduler final : public ayt::task::ITaskScheduler {
    ayt::task::TaskScheduler _scheduler{4};
public:
    std::size_t submitted=0;
    void submit(ayt::task::ITask* task) override {++submitted;_scheduler.submit(task);}
    bool submitOwned(std::shared_ptr<ayt::task::ITask> task) override {const bool accepted=_scheduler.submitOwned(std::move(task));submitted+=accepted;return accepted;}
    void submit(std::initializer_list<ayt::task::ITask*> tasks) override {submitted+=tasks.size();_scheduler.submit(tasks);}
    void wait(ayt::task::ITask* task) override {_scheduler.wait(task);}
    void waitAll() override {_scheduler.waitAll();}
    void shutdown() override {_scheduler.shutdown();}
    bool isShuttingDown() const override {return _scheduler.isShuttingDown();}
    std::size_t numThreads() const override {return _scheduler.numThreads();}
    std::size_t numQueuedTasks() const override {return _scheduler.numQueuedTasks();}
    std::size_t numRunningTasks() const override {return _scheduler.numRunningTasks();}
};
BatchInput input(std::shared_ptr<const CalculationPlan> plan,std::size_t n) {
    BatchInput result;result.plan=std::move(plan);result.columns.resize(2);result.requests.resize(n);result.contexts.resize(n);
    for(auto& column:result.columns)column.resize(n);
    for(std::size_t i=0;i<n;++i){result.requests[i]=i+1;result.columns[0][i]=i%10000;result.columns[1][i]=15001;result.contexts[i].calculation=i+1;}
    return result;
}
int main(int argc,char** argv) {
    try {
    const auto n=argc>1?std::stoull(argv[1]):1000000ull;if(!n || n>1000000)throw std::runtime_error("count must be 1..1000000");
    auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Input,.binding=1},{.id=3,.op=Op::MultiplyRate,.a=1,.b=2}},3,2).value());
    CountingScheduler scheduler;
#ifdef AY_STATS_METRICS_GPU
    auto device=GpuExecutor::create();if(!device)throw std::runtime_error(device.error().message);
    {auto warm=(*device)->execute(input(plan,n),BackendChoice::Gpu);if(!warm)throw std::runtime_error(warm.error().message);}
#endif
    // Warm CPU queues/scratch outside the recorded region.
    {auto warm=BatchExecutor::submit({input(plan,1000)},scheduler).value();(void)warm.finish().value();}
    std::cout<<"requests,backend,construction_ms,submit_validate_pack_ms,finish_wait_readback_ms,cxx_new_calls,peak_incremental_cxx_bytes,submitted_tasks,simd_lanes,transfer_bytes\n";
    std::vector<Value> reference;
    for(int backend=0;backend<
#ifdef AY_STATS_METRICS_GPU
        2
#else
        1
#endif
        ;++backend) {
        scheduler.submitted=0;const auto base=metrics::reset();const auto a=metrics::Clock::now();auto data=input(plan,n);const auto b=metrics::Clock::now();
        BatchOutput output;metrics::Clock::time_point c;
        if(backend==0){std::vector<BatchInput> owned;owned.push_back(std::move(data));auto job=BatchExecutor::submit(std::move(owned),scheduler);if(!job)throw std::runtime_error(job.error().message);c=metrics::Clock::now();output=std::move(job->finish().value()[0]);}
#ifdef AY_STATS_METRICS_GPU
        else {auto job=(*device)->submit(std::move(data));if(!job)throw std::runtime_error(job.error().message);c=metrics::Clock::now();output=job->finish().value();}
#endif
        const auto d=metrics::Clock::now();const auto calls=metrics::calls.load(),peak=metrics::peak.load()-base;
        if(output.failed || output.cancelled)throw std::runtime_error("failed/cancelled output");
        if(backend==0)reference=output.values;else if(reference!=output.values)throw std::runtime_error("result mismatch");
        std::cout<<n<<','<<(backend?"gpu-warm":"cpu-4")<<','<<metrics::ms(a,b)<<','<<metrics::ms(b,c)<<','<<metrics::ms(c,d)<<','<<calls<<','<<peak<<','<<scheduler.submitted<<','<<output.simdLanes<<','<<(backend?n*12:0)<<'\n';
    }
    scheduler.shutdown();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
