#pragma once
#include <AYStats/Batch.h>
struct ID3D11Device;
namespace ayt::stats {
struct GpuQualification { bool supported=false; std::string reason; std::size_t transferBytes=0; };
struct GpuDeviceInfo {std::string name;std::uint32_t vendor=0,device=0;std::uint64_t dedicatedBytes=0;};
/// Single submission-thread asynchronous result. Owns all device resources until released.
class GpuBatchJob {
public:
    GpuBatchJob();
    ~GpuBatchJob();
    GpuBatchJob(GpuBatchJob&&) noexcept;
    GpuBatchJob& operator=(GpuBatchJob&&) noexcept;
    GpuBatchJob(const GpuBatchJob&)=delete;
    /// Non-blocking event query; device failure is reported separately from pending work.
    Result<bool> ready();
    /// Cancel result delivery only; submitted GPU commands continue safely.
    void cancel() noexcept;
    /// Bounded wait/readback; single consumption. No implicit delayed simulation tick.
    Result<BatchOutput> finish(std::uint32_t timeoutMs=10000);
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
    friend class GpuExecutor;
};
enum class BackendChoice { Cpu, Gpu, Automatic };
struct BackendResult { BatchOutput output; bool usedGpu=false; std::string reason; };
/// Optional D3D11 strict integer backend. No device/thread/GPU dependency enters StatsCore.
/// Every intermediate arithmetic product must be provably signed-32-bit safe for this profile.
class GpuExecutor {
public:
    ~GpuExecutor();
    /// Reuse a Host device or create an isolated compute device; hardware only, no silent WARP.
    static Result<std::unique_ptr<GpuExecutor>> create(ID3D11Device* hostDevice=nullptr);
    /// Shape, identity, range and instruction qualification; never evaluates every lane on CPU.
    static Result<GpuQualification> qualify(const BatchInput& input);
    GpuDeviceInfo deviceInfo() const;
    Result<GpuBatchJob> submit(BatchInput input);
    /// Automatic stays CPU until a caller supplies a measured per-recipe threshold.
    Result<BackendResult> execute(BatchInput input,BackendChoice choice=BackendChoice::Automatic,
        std::optional<std::size_t> measuredThreshold={},ayt::task::ITaskScheduler* scheduler=nullptr);
private:
    GpuExecutor();
    struct Impl;
    std::shared_ptr<Impl> _impl;
    friend class GpuBatchJob;
};
} // namespace ayt::stats
