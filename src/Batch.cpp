#include <AYStats/Batch.h>
#include <AYTask/ITaskScheduler.h>
#include <AYTask/LambdaTask.h>
#include <algorithm>
#include <unordered_set>
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
Result<BatchSettlement> settleBatch(std::span<StatSet* const> bank,std::span<const SettlementRequest> requests,BatchSemantics semantics,std::span<const std::uint64_t> versions,ayt::task::ITaskScheduler* scheduler,SettlementLimits limits) {
    if(bank.size()!=versions.size() || bank.size()>65536 || requests.size()>1000000)return fail(Error::Limit,"bank/transaction limit mismatch");
    if(semantics!=BatchSemantics::Snapshot && semantics!=BatchSemantics::Ordered)return fail(Error::Invalid,"unknown batch semantics");
    std::size_t bytes=0,operations=0;
    const auto reserve=[&](std::size_t count,std::size_t stride){if(!stride || bytes>limits.memoryBudget || count>(limits.memoryBudget-bytes)/stride)return false;bytes+=count*stride;return true;};
    if(!reserve(requests.size(),96))return fail(Error::Limit,"settlement request workspace budget");
    for(auto* state:bank){if(!state)return fail(Error::Invalid,"null settlement state");if(!reserve(state->rows().size(),sizeof(Row)) || !reserve(state->modifiers().size(),sizeof(Modifier)))return fail(Error::Limit,"settlement shadow budget");}
    for(const auto& request:requests){if(request.writes.size()>limits.maxOperations-operations)return fail(Error::Limit,"settlement operation limit");operations+=request.writes.size();if(!reserve(request.writes.size(),128) || !reserve(request.inputs.size(),16))return fail(Error::Limit,"settlement result/access budget");}
    std::unordered_set<StatSet*> pointers;std::vector<StatSet> shadow;shadow.reserve(bank.size());
    for(std::size_t i=0;i<bank.size();++i) {
        if(!bank[i] || !pointers.insert(bank[i]).second)return fail(Error::Duplicate,"null/duplicate bank state");
        if(bank[i]->version()!=versions[i])return fail(Error::Stale,"bank version mismatch");shadow.push_back(*bank[i]);
    }
    const auto n=requests.size();
    std::vector<std::size_t> parent(n);std::iota(parent.begin(),parent.end(),std::size_t(0));
    const auto root=[&](std::size_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    std::vector<std::vector<std::size_t>> accesses(bank.size());std::vector<std::uint8_t> written(bank.size());
    std::unordered_set<std::uint64_t> ids;
    for(std::size_t i=0;i<n;++i) {
        const auto& request=requests[i];
        if(!request.id || !ids.insert(request.id).second)return fail(Error::Duplicate,"request identity reused");
        if(!request.plan || request.inputs.size()!=request.plan->inputCount())return fail(Error::Invalid,"settlement input shape");
        for(const auto& binding:request.inputs)if(binding.state!=SIZE_MAX){if(binding.state>=bank.size())return fail(Error::Missing,"input state outside bank");accesses[binding.state].push_back(i);}
        for(const auto& target:request.writes){if(target.state>=bank.size())return fail(Error::Missing,"write state outside bank");accesses[target.state].push_back(i);written[target.state]=1;}
    }
    // Read/read sharing does not conflict; any writer conservatively connects all users of a StatSet.
    for(std::size_t state=0;state<bank.size();++state)if(written[state] && !accesses[state].empty())
        for(auto index:accesses[state]){auto a=root(accesses[state].front()),b=root(index);if(a!=b)parent[b]=a;}
    std::vector<std::pair<std::size_t,std::size_t>> order;order.reserve(n);
    for(std::size_t i=0;i<n;++i)order.emplace_back(root(i),i);std::sort(order.begin(),order.end());
    std::vector<std::pair<std::size_t,std::size_t>> groups;
    for(std::size_t start=0;start<n;){auto end=start+1;while(end<n && order[end].first==order[start].first)++end;groups.emplace_back(start,end);start=end;}
    BatchSettlement output;output.values.resize(n);output.results.resize(n);output.conflictGroups=groups.size();
    std::vector<std::optional<Diagnostic>> errors(groups.size());
    const auto evaluateRequest=[&](std::size_t index)->Result<void> {
        const auto& request=requests[index];
        std::vector<Value> input,scratch(request.plan->instructions().size());input.reserve(request.inputs.size());
        for(const auto& binding:request.inputs) {
            if(binding.state==SIZE_MAX){input.push_back(binding.literal);continue;}
            if(binding.state>=bank.size())return fail(Error::Missing,"input state outside bank");
            const auto& state=semantics==BatchSemantics::Ordered?shadow[binding.state]:*bank[binding.state];
            auto row=state.read(binding.stat);if(!row)return std::unexpected(row.error());
            switch(binding.field){case ReadField::Effective:input.push_back(row->effective);break;case ReadField::Current:input.push_back(row->current);break;case ReadField::Capacity:input.push_back(row->capacity);break;case ReadField::Base:input.push_back(row->base);break;default:return fail(Error::Invalid,"unknown read field");}
        }
        auto value=request.plan->evaluate(input,request.context,scratch);if(!value)return std::unexpected(value.error());
        output.values[index]=*value;std::vector<Settlement> results;
        for(const auto& target:request.writes) {
            if(target.state>=bank.size())return fail(Error::Missing,"write state outside bank");
            auto& state=shadow[target.state];Mutation op{target.stat,target.op,*value};auto result=state.commit(std::span(&op,1),state.version());
            if(!result)return std::unexpected(result.error());results.push_back(std::move(*result));
        }
        output.results[index]=std::move(results);return {};
    };
    const auto executeGroups=[&](std::size_t begin,std::size_t end) {
        for(auto g=begin;g<end;++g) {
            for(auto pos=groups[g].first;pos<groups[g].second;++pos) {
                try {auto r=evaluateRequest(order[pos].second);if(!r){errors[g]=r.error();break;}}
                catch(const std::exception& e){errors[g]=Diagnostic{Error::Invalid,e.what(),{}};break;}
                catch(...){errors[g]=Diagnostic{Error::Invalid,"settlement extension exception",{}};break;}
            }
        }
    };
    if(scheduler && groups.size()>1) {
        std::vector<std::shared_ptr<ayt::task::ITask>> tasks;
        const auto chunk=std::max<std::size_t>(64,groups.size()/4096+1);
        for(std::size_t begin=0;begin<groups.size();begin+=chunk) {
            auto lambda=[&,begin,end=std::min(groups.size(),begin+chunk)]{executeGroups(begin,end);};
            auto task=std::shared_ptr<ayt::task::ITask>(new ayt::task::LambdaTask<decltype(lambda)>(std::move(lambda),"AYStats.Settlement"));
            if(!scheduler->submitOwned(task)){for(const auto& submitted:tasks)scheduler->wait(submitted.get());return fail(Error::Unsupported,"settlement scheduler refused work");}
            tasks.push_back(std::move(task));
        }
        output.parallelTasks=tasks.size();for(const auto& task:tasks)scheduler->wait(task.get());
    } else executeGroups(0,groups.size());
    // Pick the earliest failing original request group deterministically.
    std::optional<std::pair<std::size_t,Diagnostic>> first;
    for(std::size_t g=0;g<groups.size();++g)if(errors[g]) {
        const auto index=order[groups[g].first].second;if(!first || index<first->first)first=std::pair{index,*errors[g]};
    }
    if(first)return std::unexpected(first->second);
    // Single owner: revalidate all inputs before publishing any cross-object state.
    for(std::size_t i=0;i<bank.size();++i)if(bank[i]->version()!=versions[i])return fail(Error::Stale,"bank changed before publication");
    for(std::size_t i=0;i<bank.size();++i)*bank[i]=std::move(shadow[i]);return output;
}
} // namespace ayt::stats
