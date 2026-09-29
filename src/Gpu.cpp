#include <AYStats/Gpu.h>
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <sstream>
#include <unordered_map>

namespace ayt::stats {
namespace {
using Microsoft::WRL::ComPtr;
auto fail(Error e,std::string m){return std::unexpected(Diagnostic{e,std::move(m),{}});}
Result<void> checked(HRESULT result,const char* action){if(FAILED(result))return fail(Error::Unsupported,std::string(action)+" HRESULT="+std::to_string(std::uint32_t(result)));return {};}
struct Range {Value lo=0,hi=0;};
bool safe(const Range& r){return r.lo>=INT32_MIN && r.hi<=INT32_MAX;}
Result<GpuQualification> qualification(const BatchInput& input) {
    const auto n=input.requests.size();
    if(!input.plan || input.columns.size()!=input.plan->inputCount() || input.contexts.size()!=n)return fail(Error::Invalid,"GPU bucket shape mismatch");
    if(!n || n>1000000 || input.columns.size()>256 || input.plan->instructions().size()>256)return GpuQualification{false,"GPU profile count/input/instruction limit",0};
    const auto bytes=n*(input.columns.size()+1)*sizeof(std::int32_t);
    if(n*(input.columns.size()+3)*4>128ull*1024*1024)return GpuQualification{false,"GPU transfer/resource budget",bytes};
    std::vector<std::uint64_t> ids=input.requests;std::sort(ids.begin(),ids.end());
    if(!ids.front() || std::adjacent_find(ids.begin(),ids.end())!=ids.end())return fail(Error::Duplicate,"GPU request identity reused");
    std::vector<Range> inputs;
    for(const auto& column:input.columns){if(column.size()!=n)return fail(Error::Invalid,"GPU column length mismatch");const auto bounds=std::minmax_element(column.begin(),column.end());inputs.push_back({*bounds.first,*bounds.second});if(!safe(inputs.back()))return GpuQualification{false,"input outside strict32 range",bytes};}
    std::vector<Range> ranges;
    for(const auto& inst:input.plan->instructions()) {
        const auto& node=inst.node;Range r,a,b,c;
        if(node.op==Op::Table || node.op==Op::Random || node.op==Op::Extension)
            return GpuQualification{false,"instruction is CPU-only in strict32 profile",bytes};
        if(node.op!=Op::Input && node.op!=Op::Constant && node.op!=Op::Random){a=ranges[inst.a];b=ranges[inst.b];if(node.op==Op::Clamp || node.op==Op::Select)c=ranges[inst.c];}
        switch(node.op) {
        case Op::Input:r=inputs[node.binding];break;
        case Op::Constant:r={node.value,node.value};break;
        case Op::Add:r={a.lo+b.lo,a.hi+b.hi};break;
        case Op::Subtract:r={a.lo-b.hi,a.hi-b.lo};break;
        case Op::Multiply:case Op::MultiplyRate: {
            const std::array<Value,4> products{a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};const auto limits=std::minmax_element(products.begin(),products.end());r={*limits.first,*limits.second};
            if(!safe(r))return GpuQualification{false,"product requires widened CPU arithmetic",bytes};
            if(node.op==Op::MultiplyRate){r.lo/=RateScale;r.hi/=RateScale;}break;
        }
        case Op::Divide: {
            if(b.lo<=0 && b.hi>=0)return GpuQualification{false,"divisor interval includes zero",bytes};
            const std::array<Value,4> quotients{a.lo/b.lo,a.lo/b.hi,a.hi/b.lo,a.hi/b.hi};const auto limits=std::minmax_element(quotients.begin(),quotients.end());r={*limits.first,*limits.second};break;
        }
        case Op::Minimum:r={std::min(a.lo,b.lo),std::min(a.hi,b.hi)};break;
        case Op::Maximum:r={std::max(a.lo,b.lo),std::max(a.hi,b.hi)};break;
        case Op::Clamp:if(b.hi>c.lo)return GpuQualification{false,"clamp interval not provably ordered",bytes};r={std::min(std::max(a.lo,b.lo),c.lo),std::min(std::max(a.hi,b.hi),c.hi)};break;
        case Op::Greater:r={0,1};break;
        case Op::Select:r={std::min(b.lo,c.lo),std::max(b.hi,c.hi)};break;
        default:return GpuQualification{false,"instruction is CPU-only in strict32 profile",bytes};
        }
        if(!safe(r))return GpuQualification{false,"intermediate outside strict32 range",bytes};ranges.push_back(r);
    }
    return GpuQualification{true,"strict32 proven",bytes};
}
std::string shaderSource(const CalculationPlan& plan) {
    std::ostringstream shader;shader<<"StructuredBuffer<int> inputValues:register(t0);RWStructuredBuffer<int> results:register(u0);cbuffer Params:register(b0){uint count;uint3 padding;}[numthreads(256,1,1)]void main(uint3 tid:SV_DispatchThreadID){uint lane=tid.x;if(lane>=count)return;\n";
    for(std::size_t i=0;i<plan.instructions().size();++i) {
        const auto& inst=plan.instructions()[i];const auto& n=inst.node;const auto a="v"+std::to_string(inst.a),b="v"+std::to_string(inst.b),c="v"+std::to_string(inst.c);std::string expression;
        switch(n.op){
        case Op::Input:expression="inputValues["+std::to_string(n.binding)+"*count+lane]";break;
        case Op::Constant:expression=n.value==INT32_MIN?"(int)0x80000000u":std::to_string(n.value);break;
        case Op::Add:expression=a+"+"+b;break;case Op::Subtract:expression=a+"-"+b;break;
        case Op::Multiply:expression=a+"*"+b;break;case Op::Divide:expression=a+"/"+b;break;
        case Op::MultiplyRate:expression="("+a+"*"+b+")/10000";break;
        case Op::Minimum:expression="min("+a+","+b+")";break;case Op::Maximum:expression="max("+a+","+b+")";break;
        case Op::Clamp:expression="min(max("+a+","+b+"),"+c+")";break;
        case Op::Greater:expression="("+a+">"+b+"?1:0)";break;case Op::Select:expression="("+a+"!=0?"+b+":"+c+")";break;
        default:break;
        }
        shader<<"int v"<<i<<"="<<expression<<";\n";
    }
    shader<<"results[lane]=v"<<plan.outputIndex()<<";}";return shader.str();
}
}
struct GpuResources {
    ComPtr<ID3D11Buffer> input,output,staging,params;
    ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11Query> event;
    std::size_t count=0,columns=0;
};
struct GpuExecutor::Impl {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::unordered_map<std::string,ComPtr<ID3D11ComputeShader>> programs;
    std::vector<GpuResources> idle;
    std::thread::id owner=std::this_thread::get_id();
};
struct GpuBatchJob::Impl : GpuResources {
    std::shared_ptr<GpuExecutor::Impl> device;
    bool cancelled=false,consumed=false;
    ~Impl(){
        if(!consumed || !device || device->owner!=std::this_thread::get_id() || device->idle.size()>=4)return;
        std::size_t bytes=count*(columns+2)*4;for(const auto& cached:device->idle)bytes+=cached.count*(cached.columns+2)*4;
        if(bytes>128ull*1024*1024)return;
        try{device->idle.push_back(std::move(static_cast<GpuResources&>(*this)));}catch(...){/* caching is optional */}
    }
};
GpuExecutor::GpuExecutor()=default;GpuExecutor::~GpuExecutor()=default;
GpuBatchJob::GpuBatchJob()=default;GpuBatchJob::~GpuBatchJob()=default;GpuBatchJob::GpuBatchJob(GpuBatchJob&&) noexcept=default;GpuBatchJob& GpuBatchJob::operator=(GpuBatchJob&&) noexcept=default;
Result<std::unique_ptr<GpuExecutor>> GpuExecutor::create(ID3D11Device* host) {
    auto executor=std::unique_ptr<GpuExecutor>(new GpuExecutor);executor->_impl=std::make_shared<Impl>();auto& i=*executor->_impl;
    if(host){i.device=host;host->GetImmediateContext(&i.context);}
    else {D3D_FEATURE_LEVEL actual;const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};if(auto r=checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,D3D11_SDK_VERSION,&i.device,&actual,&i.context),"D3D11CreateDevice");!r)return std::unexpected(r.error());}
    if(i.device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0)return fail(Error::Unsupported,"strict GPU needs feature level 11");return executor;
}
Result<GpuQualification> GpuExecutor::qualify(const BatchInput& input){return qualification(input);}
GpuDeviceInfo GpuExecutor::deviceInfo() const {
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
    if(FAILED(_impl->device.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc)))return {};
    const auto size=WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,nullptr,0,nullptr,nullptr);std::string name(size?size:0,'\0');
    if(size>1){WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name.data(),size,nullptr,nullptr);name.resize(size-1);}
    return {std::move(name),desc.VendorId,desc.DeviceId,std::uint64_t(desc.DedicatedVideoMemory)};
}
Result<GpuBatchJob> GpuExecutor::submit(BatchInput input) {
    auto qualified=qualify(input);if(!qualified)return std::unexpected(qualified.error());if(!qualified->supported)return fail(Error::Unsupported,qualified->reason);
    auto& device=*_impl;if(device.owner!=std::this_thread::get_id())return fail(Error::Invalid,"GPU submission thread mismatch");
    const auto source=shaderSource(*input.plan);
    auto program=device.programs.find(source);
    if(program==device.programs.end()) {
        ComPtr<ID3DBlob> bytes,errors;
        const auto hr=D3DCompile(source.data(),source.size(),"AYStats",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytes,&errors);
        if(FAILED(hr))return fail(Error::Unsupported,errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"compute shader compile failed");
        ComPtr<ID3D11ComputeShader> shader;if(auto r=checked(device.device->CreateComputeShader(bytes->GetBufferPointer(),bytes->GetBufferSize(),nullptr,&shader),"CreateComputeShader");!r)return std::unexpected(r.error());
        if(device.programs.size()>=64)device.programs.clear();program=device.programs.emplace(source,std::move(shader)).first;
    }
    GpuBatchJob job;job._impl=std::make_unique<GpuBatchJob::Impl>();auto& i=*job._impl;i.device=_impl;i.count=input.requests.size();i.columns=input.columns.size();
    auto reusable=std::find_if(device.idle.begin(),device.idle.end(),[&](const auto& item){return item.count==i.count && item.columns==i.columns;});
    if(reusable!=device.idle.end()){static_cast<GpuResources&>(i)=std::move(*reusable);device.idle.erase(reusable);}
    std::vector<std::int32_t> packed;packed.reserve(std::max<std::size_t>(1,i.count*input.columns.size()));for(const auto& column:input.columns)for(auto value:column)packed.push_back(std::int32_t(value));if(packed.empty())packed.push_back(0);
    if(!i.input) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(packed.size()*sizeof(std::int32_t));desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=4;
    D3D11_SUBRESOURCE_DATA data{packed.data(),0,0};if(auto r=checked(device.device->CreateBuffer(&desc,&data,&i.input),"input buffer");!r)return std::unexpected(r.error());
    if(auto r=checked(device.device->CreateShaderResourceView(i.input.Get(),nullptr,&i.srv),"input SRV");!r)return std::unexpected(r.error());
    desc.ByteWidth=UINT(i.count*4);desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    if(auto r=checked(device.device->CreateBuffer(&desc,nullptr,&i.output),"output buffer");!r)return std::unexpected(r.error());
    if(auto r=checked(device.device->CreateUnorderedAccessView(i.output.Get(),nullptr,&i.uav),"output UAV");!r)return std::unexpected(r.error());
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
    if(auto r=checked(device.device->CreateBuffer(&desc,nullptr,&i.staging),"readback buffer");!r)return std::unexpected(r.error());
    const std::array<UINT,4> parameters{UINT(i.count),0,0,0};desc={};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;data={parameters.data(),0,0};
    if(auto r=checked(device.device->CreateBuffer(&desc,&data,&i.params),"parameter buffer");!r)return std::unexpected(r.error());
    const D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};if(auto r=checked(device.device->CreateQuery(&query,&i.event),"completion query");!r)return std::unexpected(r.error());
    } else device.context->UpdateSubresource(i.input.Get(),0,nullptr,packed.data(),0,0);
    auto* srv=i.srv.Get();auto* uav=i.uav.Get();auto* params=i.params.Get();device.context->CSSetShader(program->second.Get(),nullptr,0);device.context->CSSetShaderResources(0,1,&srv);device.context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);device.context->CSSetConstantBuffers(0,1,&params);
    device.context->Dispatch(UINT((i.count+255)/256),1,1);
    ID3D11UnorderedAccessView* emptyUav=nullptr;ID3D11ShaderResourceView* emptySrv=nullptr;device.context->CSSetUnorderedAccessViews(0,1,&emptyUav,nullptr);device.context->CSSetShaderResources(0,1,&emptySrv);
    device.context->CopyResource(i.staging.Get(),i.output.Get());device.context->End(i.event.Get());device.context->Flush();return job;
}
Result<bool> GpuBatchJob::ready() {
    if(!_impl || _impl->consumed)return fail(Error::Invalid,"empty/consumed GPU job");
    if(_impl->device->owner!=std::this_thread::get_id())return fail(Error::Invalid,"GPU query thread mismatch");
    BOOL complete=FALSE;const auto hr=_impl->device->context->GetData(_impl->event.Get(),&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(FAILED(hr))return fail(Error::Unsupported,"GPU completion/device failure");return hr==S_OK && complete;
}
void GpuBatchJob::cancel() noexcept {if(_impl)_impl->cancelled=true;}
Result<BatchOutput> GpuBatchJob::finish(std::uint32_t timeoutMs) {
    if(!_impl || _impl->consumed)return fail(Error::Invalid,"empty/consumed GPU job");
    const auto started=std::chrono::steady_clock::now();
    for(;;){auto complete=ready();if(!complete)return std::unexpected(complete.error());if(*complete)break;if(std::chrono::steady_clock::now()-started>=std::chrono::milliseconds(timeoutMs))return fail(Error::Limit,"GPU wait timeout; no state was committed");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    _impl->consumed=true;if(_impl->cancelled)return fail(Error::Cancelled,"GPU result delivery cancelled");
    // Allocate before Map so allocation failure cannot leave a pooled staging buffer mapped.
    BatchOutput output;output.values.resize(_impl->count);output.status.assign(_impl->count,1);
    D3D11_MAPPED_SUBRESOURCE mapped{};auto& device=*_impl->device;if(auto r=checked(device.context->Map(_impl->staging.Get(),0,D3D11_MAP_READ,0,&mapped),"GPU readback Map");!r)return std::unexpected(r.error());
    const auto* values=static_cast<const std::int32_t*>(mapped.pData);for(std::size_t i=0;i<_impl->count;++i)output.values[i]=values[i];device.context->Unmap(_impl->staging.Get(),0);return output;
}
Result<BackendResult> GpuExecutor::execute(BatchInput input,BackendChoice choice,std::optional<std::size_t> threshold,ayt::task::ITaskScheduler* scheduler) {
    if(choice!=BackendChoice::Cpu && choice!=BackendChoice::Gpu && choice!=BackendChoice::Automatic)return fail(Error::Invalid,"invalid backend choice");
    auto q=choice==BackendChoice::Cpu?Result<GpuQualification>(GpuQualification{}):qualify(input);if(!q)return std::unexpected(q.error());
    const bool use=choice==BackendChoice::Gpu || (choice==BackendChoice::Automatic && threshold && input.requests.size()>=*threshold && q->supported);
    std::string reason=choice==BackendChoice::Cpu?"explicit CPU":!q->supported?q->reason:!threshold?"no measured automatic threshold":"below measured threshold";
    if(use) {
        auto job=submit(input);if(job){auto output=job->finish();if(output)return BackendResult{std::move(*output),true,"strict32 GPU"};if(choice==BackendChoice::Gpu)return std::unexpected(output.error());reason=output.error().message;}
        else {if(choice==BackendChoice::Gpu)return std::unexpected(job.error());reason=job.error().message;}
    }
    std::vector<BatchInput> inputs;inputs.push_back(std::move(input));
    Result<std::vector<BatchOutput>> output=std::unexpected(Diagnostic{});
    if(scheduler){auto job=BatchExecutor::submit(std::move(inputs),*scheduler);if(!job)return std::unexpected(job.error());output=job->finish();}else output=BatchExecutor::scalar(std::move(inputs));
    if(!output)return std::unexpected(output.error());return BackendResult{std::move((*output)[0]),false,std::move(reason)};
}
} // namespace ayt::stats
