#include <AYStats/Batch.h>
#include <AYTask/ITaskScheduler.h>
#include <AYTask/LambdaTask.h>
#include <algorithm>
#include <chrono>
#include <bit>
#include <numeric>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif

namespace ayt::stats {
namespace {
auto fail(Error e,std::string m){return std::unexpected(Diagnostic{e,std::move(m),{}});}
struct Block {std::size_t group=0,begin=0,end=0;std::vector<std::pair<std::size_t,Diagnostic>> errors;std::size_t simdLanes=0;};
struct Work {
    std::vector<BatchInput> inputs;
    std::vector<BatchOutput> outputs;
    std::vector<Block> blocks;
    BatchOptions options;
    std::atomic_bool cancelled=false;
    std::atomic_size_t completed=0;
    std::atomic_bool fatalFailure=false;
};
Result<std::shared_ptr<Work>> prepare(std::vector<BatchInput> inputs,BatchOptions options) {
    if(!options.blockSize || !options.maxRequests || !options.maxTasks || !options.memoryBudget)return fail(Error::Invalid,"invalid batch limits");
    auto work=std::make_shared<Work>();work->options=options;
    std::size_t count=0,bytes=0;std::vector<std::uint64_t> identities;
    for(auto& bucket:inputs) {
        const auto n=bucket.requests.size();
        if(!bucket.plan || bucket.columns.size()!=bucket.plan->inputCount() || bucket.contexts.size()!=n)return fail(Error::Invalid,"bucket shape mismatch");
        if(n>options.maxRequests-count)return fail(Error::Limit,"batch request limit");count+=n;
        const auto perRequest=sizeof(CalculationContext)+sizeof(Value)*(bucket.columns.size()+1)+sizeof(std::uint64_t)+sizeof(std::uint8_t)+32;
        if(n>(options.memoryBudget-bytes)/perRequest)return fail(Error::Limit,"batch memory budget");bytes+=n*perRequest;
        for(const auto& column:bucket.columns)if(column.size()!=n)return fail(Error::Invalid,"SoA column length mismatch");
        identities.insert(identities.end(),bucket.requests.begin(),bucket.requests.end());
        const auto blocks=n/options.blockSize+(n%options.blockSize?1:0);
        if(blocks>options.maxTasks-work->blocks.size())return fail(Error::Limit,"batch task limit; enlarge block size");
        const auto scratchBytes=((bucket.plan->instructions().size()+bucket.columns.size())*sizeof(Value)+std::min(options.maxDiagnostics,options.blockSize)*(sizeof(Diagnostic)+sizeof(std::size_t)+512))*blocks;
        if(scratchBytes>options.memoryBudget-bytes)return fail(Error::Limit,"batch scratch budget");bytes+=scratchBytes;
        const auto group=work->outputs.size();work->outputs.push_back({std::vector<Value>(n),std::vector<std::uint8_t>(n)});
        for(std::size_t start=0;start<n;start+=options.blockSize)work->blocks.push_back({group,start,start+std::min(options.blockSize,n-start)});
    }
    std::sort(identities.begin(),identities.end());
    if((!identities.empty() && !identities.front()) || std::adjacent_find(identities.begin(),identities.end())!=identities.end())return fail(Error::Duplicate,"zero/duplicate request identity");
    work->inputs=std::move(inputs);return work;
}
void executeBody(const std::shared_ptr<Work>& work,std::size_t blockIndex) {
    auto& block=work->blocks[blockIndex];const auto& bucket=work->inputs[block.group];auto& output=work->outputs[block.group];
    std::vector<Value> values(bucket.columns.size()),scratch(bucket.plan->instructions().size());
    const auto instructions=bucket.plan->instructions();
    bool fastRate=work->options.useSimd && instructions.size()<=3 && !instructions.empty() && instructions.back().node.op==Op::MultiplyRate;
    if(fastRate)for(std::size_t i=0;i+1<instructions.size();++i)if(instructions[i].node.op!=Op::Input && instructions[i].node.op!=Op::Constant)fastRate=false;
    const auto operand=[&](std::size_t node,std::size_t lane){const auto& n=instructions[node].node;return n.op==Op::Constant?n.value:bucket.columns[n.binding][lane];};
    for(auto i=block.begin;i<block.end;++i) {
        if(work->cancelled.load(std::memory_order_relaxed)){output.status[i]=3;continue;}
#if defined(_M_X64) || defined(__x86_64__)
        if(fastRate && i+1<block.end) {
            const auto& final=instructions.back();const auto a=operand(final.a,i),b=operand(final.b,i),c=operand(final.a,i+1),d=operand(final.b,i+1);
            if(a>=0 && b>=0 && c>=0 && d>=0 && std::uint64_t(a)<=UINT32_MAX && std::uint64_t(b)<=UINT32_MAX && std::uint64_t(c)<=UINT32_MAX && std::uint64_t(d)<=UINT32_MAX) {
                const auto x=_mm_set_epi32(0,std::bit_cast<std::int32_t>(std::uint32_t(c)),0,std::bit_cast<std::int32_t>(std::uint32_t(a)));
                const auto y=_mm_set_epi32(0,std::bit_cast<std::int32_t>(std::uint32_t(d)),0,std::bit_cast<std::int32_t>(std::uint32_t(b)));
                std::uint64_t products[2];_mm_storeu_si128(reinterpret_cast<__m128i*>(products),_mm_mul_epu32(x,y));
                output.values[i]=Value(products[0]/RateScale);output.values[i+1]=Value(products[1]/RateScale);output.status[i]=output.status[i+1]=1;block.simdLanes+=2;++i;continue;
            }
        }
#endif
        for(std::size_t c=0;c<values.size();++c)values[c]=bucket.columns[c][i];
        auto result=bucket.plan->evaluate(values,bucket.contexts[i],scratch);
        if(result){output.values[i]=*result;output.status[i]=1;}
        else {output.status[i]=2;if(block.errors.size()<work->options.maxDiagnostics){auto diagnostic=result.error();diagnostic.message.resize(std::min<std::size_t>(256,diagnostic.message.size()));diagnostic.path.resize(std::min<std::size_t>(256,diagnostic.path.size()));block.errors.emplace_back(i,std::move(diagnostic));}}
    }
}
void execute(const std::shared_ptr<Work>& work,std::size_t blockIndex) noexcept {
    try { executeBody(work,blockIndex); }
    catch(...) {
        work->fatalFailure.store(true,std::memory_order_relaxed);
        const auto& block=work->blocks[blockIndex];auto& output=work->outputs[block.group];
        for(auto lane=block.begin;lane<block.end;++lane)if(output.status[lane]==0)output.status[lane]=2;
        // Even allocation failure completes the block; ready/finish cannot hang.
    }
    work->completed.fetch_add(1,std::memory_order_release);
}
std::vector<BatchOutput> collect(Work& work) {
    for(auto& block:work.blocks) {
        auto& output=work.outputs[block.group];
        output.simdLanes+=block.simdLanes;
        for(auto& error:block.errors)if(output.errors.size()<work.options.maxDiagnostics)output.errors.push_back(std::move(error));
    }
    for(auto& output:work.outputs) {
        output.failed=std::count(output.status.begin(),output.status.end(),std::uint8_t(2));
        output.cancelled=std::count(output.status.begin(),output.status.end(),std::uint8_t(3));
    }
    return std::move(work.outputs);
}
}
struct BatchJob::Impl {
    std::shared_ptr<Work> work;
    ayt::task::ITaskScheduler* scheduler=nullptr;
    std::vector<std::shared_ptr<ayt::task::ITask>> tasks;
    bool finished=false;
};
BatchJob::~BatchJob(){cancel();}
BatchJob& BatchJob::operator=(BatchJob&& other) noexcept {if(this!=&other){cancel();_impl=std::move(other._impl);}return *this;}
void BatchJob::cancel() noexcept {if(_impl && !_impl->finished)_impl->work->cancelled.store(true,std::memory_order_relaxed);}
bool BatchJob::ready() const noexcept {return _impl && _impl->work->completed.load(std::memory_order_acquire)==_impl->work->blocks.size();}
Result<std::vector<BatchOutput>> BatchJob::finish() {
    if(!_impl || _impl->finished)return fail(Error::Invalid,"batch result already consumed or empty job");
    for(const auto& task:_impl->tasks)_impl->scheduler->wait(task.get());
    _impl->finished=true;_impl->tasks.clear();if(_impl->work->fatalFailure.load())return fail(Error::Limit,"batch worker failed while allocating/evaluating its block");return collect(*_impl->work);
}
Result<BatchJob> BatchExecutor::submit(std::vector<BatchInput> inputs,ayt::task::ITaskScheduler& scheduler,BatchOptions options) {
    auto work=prepare(std::move(inputs),options);if(!work)return std::unexpected(work.error());
    BatchJob job;job._impl=std::make_shared<BatchJob::Impl>();job._impl->work=*work;job._impl->scheduler=&scheduler;
    for(std::size_t i=0;i<(*work)->blocks.size();++i) {
        auto lambda=[owned=*work,i]{execute(owned,i);};
        auto task=std::shared_ptr<ayt::task::ITask>(new ayt::task::LambdaTask<decltype(lambda)>(std::move(lambda),"AYStats.Batch"));
        if(!scheduler.submitOwned(task)) {
            job.cancel();for(const auto& submitted:job._impl->tasks)scheduler.wait(submitted.get());
            return fail(Error::Unsupported,"scheduler refused owned work; no live state was modified");
        }
        job._impl->tasks.push_back(std::move(task));
    }
    return job;
}
Result<std::vector<BatchOutput>> BatchExecutor::scalar(std::vector<BatchInput> inputs,BatchOptions options) {
    options.useSimd=false;
    auto work=prepare(std::move(inputs),options);if(!work)return std::unexpected(work.error());
    for(std::size_t i=0;i<(*work)->blocks.size();++i)execute(*work,i);if((*work)->fatalFailure.load())return fail(Error::Limit,"scalar batch block allocation failure");return collect(**work);
}
Result<SettlementRequest> prepareRecipeRequest(std::shared_ptr<const RecipeBook> recipes,const AttributeRequest& action) {
    if(!action.id || !action.stat || action.operation<AttributeOp::Increase || action.operation>AttributeOp::Set ||
       (action.operation!=AttributeOp::Set && action.amount<0))return fail(Error::Invalid,"invalid recipe action identity/operation/amount");
    const Recipe* selected=nullptr;
    if(recipes){auto r=recipes->select(action.stat,action.operation,action.recipe);if(!r)return std::unexpected(r.error());selected=*r;}
    else if(!action.recipe.empty())return fail(Error::Missing,"no recipes configured");
    SettlementRequest request;request.id=action.id;request.context=action.context;
    if(selected) {
        request.plan=std::shared_ptr<const CalculationPlan>(std::move(recipes),&selected->plan);
        for(const auto& input:selected->inputs) {
            if(input.source==InputSource::Amount){request.inputs.push_back({.literal=action.amount});continue;}
            if(input.source==InputSource::Constant){request.inputs.push_back({.literal=input.constant});continue;}
            if(input.source==InputSource::SourceStat && !action.source)return fail(Error::Missing,"recipe requires a source state");
            ReadField field=ReadField::Effective;
            switch(input.field){case InputField::Effective:break;case InputField::Base:field=ReadField::Base;break;
                case InputField::Current:field=ReadField::Current;break;case InputField::Capacity:field=ReadField::Capacity;break;}
            request.inputs.push_back({input.source==InputSource::SourceStat?*action.source:action.target,input.stat,field});
        }
    } else {
        static const auto direct=std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value());
        request.plan=direct;request.inputs={{.literal=action.amount}};
    }
    request.writes.push_back({.state=action.target,.stat=action.stat,.attribute=action.operation});
    return request;
}
namespace detail {
class SettlementAccess {
public:
    static Result<Settlement> commit(StatSet& shadow,const Mutation& operation,std::vector<std::uint8_t>& scratch) {
        return shadow.commitShadow(std::span(&operation,1),shadow.version(),scratch);
    }
};
}
Result<BatchSettlement> settleBatch(std::span<StatSet* const> bank,std::span<const SettlementRequest> requests,BatchSemantics semantics,std::span<const std::uint64_t> versions,ayt::task::ITaskScheduler* scheduler,SettlementLimits limits,SettlementMetrics* metrics) {
    using Clock=std::chrono::steady_clock;
    if(metrics)*metrics={};
    const auto stamp=[&]{return metrics?Clock::now():Clock::time_point{};};
    const auto elapsed=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
    auto phase=stamp();
    try {
        if(bank.size()!=versions.size() || bank.size()>65536 || requests.size()>1000000)return fail(Error::Limit,"bank/transaction limit mismatch");
        if(semantics!=BatchSemantics::Snapshot && semantics!=BatchSemantics::Ordered)return fail(Error::Invalid,"unknown batch semantics");
        const auto n=requests.size();std::size_t bytes=0,operations=0,maxInputs=0,maxInstructions=0,maxRows=0;
        const auto reserve=[&](std::size_t count,std::size_t stride){if(!stride || bytes>limits.memoryBudget || count>(limits.memoryBudget-bytes)/stride)return false;bytes+=count*stride;return true;};
        // Outputs, sorted IDs and stable request/group indices. Input ownership is caller-side.
        if(!reserve(n,sizeof(Value)+sizeof(std::vector<Settlement>)+3*sizeof(std::size_t)))return fail(Error::Limit,"settlement request workspace budget");
        if(!reserve(bank.size(),sizeof(StatSet)+sizeof(StatSet*)+4*sizeof(std::size_t)+sizeof(std::uint8_t)))return fail(Error::Limit,"settlement bank workspace budget");
        for(auto* state:bank){
            if(!state)return fail(Error::Invalid,"null settlement state");
            if(!reserve(state->rows().size(),sizeof(Row)) || !reserve(state->modifiers().size(),sizeof(Modifier)))return fail(Error::Limit,"settlement shadow budget");
            maxRows=std::max(maxRows,state->rows().size());
        }
        std::vector<StatSet*> pointers(bank.begin(),bank.end());std::sort(pointers.begin(),pointers.end(),std::less<StatSet*>{});
        if(std::adjacent_find(pointers.begin(),pointers.end())!=pointers.end())return fail(Error::Duplicate,"duplicate bank state");
        for(std::size_t i=0;i<bank.size();++i)if(bank[i]->version()!=versions[i])return fail(Error::Stale,"bank version mismatch");
        std::vector<std::uint64_t> ids;ids.reserve(n);std::vector<std::uint8_t> written(bank.size());
        for(const auto& request:requests) {
            if(!request.id)return fail(Error::Duplicate,"zero request identity");ids.push_back(request.id);
            if(!request.plan || request.inputs.size()!=request.plan->inputCount())return fail(Error::Invalid,"settlement input shape");
            if(request.writes.size()>limits.maxOperations-operations)return fail(Error::Limit,"settlement operation limit");operations+=request.writes.size();
            // One owned Settlement vector and up to two Change records per write.
            if(!reserve(request.writes.size(),sizeof(Settlement)+2*sizeof(Change)))return fail(Error::Limit,"settlement result budget");
            maxInputs=std::max(maxInputs,request.inputs.size());maxInstructions=std::max(maxInstructions,request.plan->instructions().size());
            for(const auto& binding:request.inputs)if(binding.state!=SIZE_MAX && binding.state>=bank.size())return fail(Error::Missing,"input state outside bank");
            for(const auto& target:request.writes){if(target.state>=bank.size())return fail(Error::Missing,"write state outside bank");written[target.state]=1;}
        }
        std::sort(ids.begin(),ids.end());
        if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return fail(Error::Duplicate,"request identity reused");
        auto now=stamp();if(metrics)metrics->validationMs=elapsed(phase,now);phase=now;
        // Union written objects, not requests. Sharing an exclusively read-only source is safe.
        std::vector<std::size_t> parent(bank.size());std::iota(parent.begin(),parent.end(),std::size_t(0));
        const auto root=[&](std::size_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
        const auto accessedWriter=[&](const SettlementRequest& request,auto&& visit){
            for(const auto& binding:request.inputs)if(binding.state!=SIZE_MAX && written[binding.state])visit(binding.state);
            for(const auto& target:request.writes)visit(target.state);
        };
        for(const auto& request:requests) {
            std::size_t first=SIZE_MAX;
            accessedWriter(request,[&](std::size_t state){const auto r=root(state);if(first==SIZE_MAX)first=r;else if(r!=first)parent[r]=first;});
        }
        std::vector<std::size_t> requestGroups(n),stateGroups(bank.size(),SIZE_MAX),counts;
        for(std::size_t i=0;i<n;++i) {
            std::size_t writer=SIZE_MAX;accessedWriter(requests[i],[&](std::size_t state){writer=root(state);});
            std::size_t group;
            if(writer==SIZE_MAX){group=counts.size();counts.push_back(0);}
            else {auto& assigned=stateGroups[writer];if(assigned==SIZE_MAX){assigned=counts.size();counts.push_back(0);}group=assigned;}
            requestGroups[i]=group;++counts[group];
        }
        const auto groupCount=counts.size();
        if(!reserve(counts.capacity(),sizeof(std::size_t)) || !reserve(groupCount,2*sizeof(std::size_t)+sizeof(std::optional<Diagnostic>)))return fail(Error::Limit,"settlement group workspace budget");
        std::vector<std::pair<std::size_t,std::size_t>> groups;groups.reserve(groupCount);
        for(std::size_t start=0,g=0;g<groupCount;++g){const auto end=start+counts[g];groups.emplace_back(start,end);counts[g]=start;start=end;}
        std::vector<std::size_t> order(n);
        for(std::size_t i=0;i<n;++i)order[counts[requestGroups[i]]++]=i;
        const auto taskLimit=scheduler?std::max<std::size_t>(1,std::min<std::size_t>(scheduler->numThreads(),64))*4:1;
        const auto chunk=std::max<std::size_t>(64,groupCount/taskLimit+(groupCount%taskLimit?1:0));
        const auto taskCount=scheduler && groupCount>1?groupCount/chunk+(groupCount%chunk?1:0):1;
        if(!reserve(taskCount,2*((maxInputs+maxInstructions)*sizeof(Value)+maxRows)+sizeof(SettlementMetrics)+256))return fail(Error::Limit,"settlement scratch budget");
        now=stamp();if(metrics)metrics->groupingMs=elapsed(phase,now);phase=now;
        std::vector<StatSet> shadow;shadow.reserve(bank.size());
        for(auto* state:bank){shadow.push_back(*state);if(metrics){++metrics->copiedStates;metrics->copiedBytes+=state->rows().size_bytes()+state->modifiers().size_bytes();}}
        now=stamp();if(metrics)metrics->cloneMs=elapsed(phase,now);phase=now;
        BatchSettlement output;output.values.resize(n);output.results.resize(n);output.conflictGroups=groupCount;
        std::vector<std::optional<Diagnostic>> errors(groupCount);
        std::atomic_bool workerAllocationFailed=false;
        std::vector<SettlementMetrics> workerMetrics(metrics?taskCount:0);
        struct Scratch {std::vector<Value> inputs,plan;std::vector<std::uint8_t> rebuild;};
        const auto evaluateRequest=[&](std::size_t index,Scratch& scratch,SettlementMetrics* probe)->Result<void> {
            const auto& request=requests[index];const auto before=stamp();
            if(probe){probe->scratchGrowths+=scratch.inputs.capacity()<request.inputs.size();probe->scratchGrowths+=scratch.plan.capacity()<request.plan->instructions().size();}
            scratch.inputs.resize(request.inputs.size());scratch.plan.resize(request.plan->instructions().size());
            for(std::size_t i=0;i<request.inputs.size();++i) {
                const auto& binding=request.inputs[i];if(binding.state==SIZE_MAX){scratch.inputs[i]=binding.literal;continue;}
                const auto& state=semantics==BatchSemantics::Ordered?shadow[binding.state]:*bank[binding.state];
                auto row=state.read(binding.stat);if(!row)return std::unexpected(row.error());
                if((binding.field==ReadField::Current || binding.field==ReadField::Capacity) && state.schema()->definitions()[*state.schema()->index(binding.stat)].shape!=Shape::Pool)return fail(Error::Invalid,"current/capacity binding requires a pool");
                switch(binding.field){case ReadField::Effective:scratch.inputs[i]=row->effective;break;case ReadField::Current:scratch.inputs[i]=row->current;break;case ReadField::Capacity:scratch.inputs[i]=row->capacity;break;case ReadField::Base:scratch.inputs[i]=row->base;break;default:return fail(Error::Invalid,"unknown read field");}
            }
            auto value=request.plan->evaluate(scratch.inputs,request.context,scratch.plan);if(!value)return std::unexpected(value.error());
            const auto evaluated=stamp();if(probe){probe->evaluationWorkerMs+=elapsed(before,evaluated);++probe->evaluatedRequests;}
            output.values[index]=*value;auto& results=output.results[index];results.reserve(request.writes.size());
            for(const auto& target:request.writes) {
                auto& state=shadow[target.state];Mutation operation{target.stat,target.op,*value};
                if(target.attribute){auto translated=attributeMutation(state,target.stat,*target.attribute,*value);if(!translated)return std::unexpected(translated.error());operation=*translated;}
                if(probe && operation.op==MutationOp::SetBase)probe->scratchGrowths+=scratch.rebuild.capacity()<state.rows().size();
                auto result=detail::SettlementAccess::commit(state,operation,scratch.rebuild);
                if(!result)return std::unexpected(result.error());results.push_back(std::move(*result));if(probe)++probe->committedWrites;
            }
            if(probe)probe->mutationWorkerMs+=elapsed(evaluated,stamp());return {};
        };
        const auto executeGroups=[&](std::size_t begin,std::size_t end,std::size_t worker) noexcept {
            try {
            Scratch scratch;auto* probe=metrics?&workerMetrics[worker]:nullptr;
            for(auto g=begin;g<end;++g)for(auto pos=groups[g].first;pos<groups[g].second;++pos) {
                try {auto result=evaluateRequest(order[pos],scratch,probe);if(!result){errors[g]=std::move(result.error());break;}}
                catch(const std::bad_alloc&){workerAllocationFailed.store(true,std::memory_order_relaxed);break;}
                catch(const std::exception& e){errors[g]=Diagnostic{Error::Invalid,e.what(),{}};break;}
                catch(...){errors[g]=Diagnostic{Error::Invalid,"settlement extension exception",{}};break;}
            }
            }catch(...){workerAllocationFailed.store(true,std::memory_order_relaxed);}
        };
        if(scheduler && groupCount>1) {
            std::vector<std::shared_ptr<ayt::task::ITask>> tasks;tasks.reserve(taskCount);
            // Allocate every task/capture before handing out references to the stack-owned bank.
            for(std::size_t begin=0;begin<groupCount;begin+=chunk) {
                auto lambda=[&,begin,end=std::min(groupCount,begin+chunk),worker=tasks.size()]{executeGroups(begin,end,worker);};
                tasks.emplace_back(new ayt::task::LambdaTask<decltype(lambda)>(std::move(lambda),"AYStats.Settlement"));
            }
            std::size_t accepted=0;
            try {
                for(const auto& task:tasks){if(!scheduler->submitOwned(task))break;++accepted;}
            }catch(...) {for(std::size_t i=0;i<accepted;++i)scheduler->wait(tasks[i].get());return fail(Error::Unsupported,"settlement scheduler submission threw");}
            for(std::size_t i=0;i<accepted;++i)scheduler->wait(tasks[i].get());
            if(accepted!=tasks.size())return fail(Error::Unsupported,"settlement scheduler refused work");
            output.parallelTasks=accepted;
        }else executeGroups(0,groupCount,0);
        now=stamp();if(metrics){metrics->executionWallMs=elapsed(phase,now);for(const auto& worker:workerMetrics){metrics->evaluationWorkerMs+=worker.evaluationWorkerMs;metrics->mutationWorkerMs+=worker.mutationWorkerMs;metrics->scratchGrowths+=worker.scratchGrowths;metrics->evaluatedRequests+=worker.evaluatedRequests;metrics->committedWrites+=worker.committedWrites;}}phase=now;
        // Groups were assigned in first-request order; retain deterministic failure priority.
        if(workerAllocationFailed.load(std::memory_order_relaxed))return fail(Error::Limit,"out of memory");
        for(const auto& error:errors)if(error)return std::unexpected(*error);
        for(std::size_t i=0;i<bank.size();++i)if(bank[i]->version()!=versions[i])return fail(Error::Stale,"bank changed before publication");
        for(std::size_t i=0;i<bank.size();++i)*bank[i]=std::move(shadow[i]);
        if(metrics)metrics->publicationMs=elapsed(phase,stamp());return output;
    }catch(const std::bad_alloc&){return fail(Error::Limit,"out of memory");}
}
} // namespace ayt::stats
