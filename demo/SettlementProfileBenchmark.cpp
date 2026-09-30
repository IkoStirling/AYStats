// Same workload for before/after builds. Allocation tracing is isolated from latency runs.
#include <AYStats/Batch.h>
#include <AYTask/TaskScheduler.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#ifdef AY_STATS_TRACE_ALLOCATIONS
#include "SettlementAllocationProbe.h"
#endif
using namespace ayt::stats;
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
int main(int argc,char** argv) {
    try {
        const std::size_t samples=argc>1?std::stoull(argv[1]):5;
        if(!samples || samples>100)throw std::runtime_error("samples must be 1..100");
        const auto schema=Schema::compile({{.name="resource",.shape=Shape::Pool,.current=100,.capacity=100}}).value();
        const auto id=statId("resource");
        const auto plan=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=1},{.id=3,.op=Op::Minimum,.a=1,.b=2}},3,1).value());
        ayt::task::TaskScheduler scheduler(4);
        std::cout<<"requests,distribution,semantics,sample,construction_ms,settlement_ms,destruction_ms,groups,tasks,checksum,new_calls,peak_incremental_cxx_bytes,validation_ms,grouping_ms,clone_ms,evaluation_worker_ms,mutation_worker_ms,execution_wall_ms,publication_ms,copied_states,scratch_growths\n";
        for(std::size_t n:{1000,100000,1000000})for(int distribution=0;distribution<3;++distribution)for(auto semantics:{BatchSemantics::Snapshot,BatchSemantics::Ordered}) {
            std::uint64_t previousChecksum=0;
            for(std::size_t run=0;run<=samples;++run) {
                const auto start=Clock::now();const auto targets=distribution==0?1:std::min<std::size_t>(n,1000);
                std::vector<StatSet> states;states.reserve(targets);
                std::vector<StatSet*> bank;bank.reserve(targets);std::vector<std::uint64_t> versions;versions.reserve(targets);
                for(std::size_t i=0;i<targets;++i)states.push_back(StatSet::create(schema).value());
                for(auto& state:states){bank.push_back(&state);versions.push_back(state.version());}
                std::vector<SettlementRequest> requests;requests.reserve(n);
                for(std::size_t i=0;i<n;++i){const auto target=i%targets,source=distribution==2?(target+1)%targets:target;
                    requests.push_back({.id=i+1,.plan=plan,.inputs={{source,id,ReadField::Current}},.writes={{target,id,MutationOp::Decrease}}});}
                const auto prepared=Clock::now();
#ifdef AY_STATS_TRACE_ALLOCATIONS
                const auto liveBefore=metrics::reset();
#endif
#if defined(AY_STATS_TRACE_ALLOCATIONS) && defined(AY_STATS_SETTLEMENT_METRICS_VERSION)
                SettlementMetrics profile;
                auto result=settleBatch(bank,requests,semantics,versions,&scheduler,{},&profile);
#else
                auto result=settleBatch(bank,requests,semantics,versions,&scheduler);
#endif
                const auto completed=Clock::now();
                if(!result)throw std::runtime_error(result.error().message);
                std::size_t calls=0,peak=0;
#ifdef AY_STATS_TRACE_ALLOCATIONS
                calls=metrics::calls.load();peak=metrics::peak.load()-liveBefore;
#endif
                std::uint64_t checksum=14695981039346656037ull;
                const auto mix=[&](std::uint64_t v){checksum=(checksum^v)*1099511628211ull;};
                for(std::size_t i=0;i<n;++i){mix(result->values[i]);for(const auto& settlement:result->results[i]){
                    mix(settlement.beforeVersion);mix(settlement.afterVersion);
                    for(const auto& c:settlement.changes){mix(c.before);mix(c.after);mix(c.actual);mix(c.excess);}}}
                for(const auto& state:states){mix(state.rows()[0].current);mix(state.version());}
                if(run && previousChecksum!=checksum)throw std::runtime_error("nondeterministic attribution");previousChecksum=checksum;
                const auto groups=result->conflictGroups,tasks=result->parallelTasks;
                const auto beforeDestruction=Clock::now();result->results.clear();result->values.clear();requests.clear();states.clear();const auto destroyed=Clock::now();
                if(!run)continue; // Warm every workload outside recorded samples.
                std::cout<<n<<','<<(distribution==0?"hotspot":distribution==1?"distributed":"cross-dependent")<<','<<(semantics==BatchSemantics::Ordered?"ordered":"snapshot")<<','<<run<<','<<milliseconds(start,prepared)<<','<<milliseconds(prepared,completed)<<','<<milliseconds(beforeDestruction,destroyed)<<','<<groups<<','<<tasks<<','<<checksum<<','<<calls<<','<<peak;
#if defined(AY_STATS_TRACE_ALLOCATIONS) && defined(AY_STATS_SETTLEMENT_METRICS_VERSION)
                std::cout<<','<<profile.validationMs<<','<<profile.groupingMs<<','<<profile.cloneMs<<','<<profile.evaluationWorkerMs<<','<<profile.mutationWorkerMs<<','<<profile.executionWallMs<<','<<profile.publicationMs<<','<<profile.copiedStates<<','<<profile.scratchGrowths;
#else
                std::cout<<",0,0,0,0,0,0,0,0,0";
#endif
                std::cout<<'\n';
            }
        }
        scheduler.shutdown();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
