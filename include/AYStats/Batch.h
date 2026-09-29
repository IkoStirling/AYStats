#pragma once
#include <AYStats/Calculation.h>
#include <atomic>
namespace ayt::task { class ITaskScheduler; }

namespace ayt::stats {
/// Homogeneous recipe bucket, owned SoA columns and stable request identities.
struct BatchInput {
    std::shared_ptr<const CalculationPlan> plan;
    std::vector<std::vector<Value>> columns;
    std::vector<CalculationContext> contexts;
    std::vector<std::uint64_t> requests;
};
struct BatchOptions {
    std::size_t blockSize=4096,maxRequests=1000000,maxTasks=4096,maxDiagnostics=256;
    std::size_t memoryBudget=256ull*1024*1024;
    bool useSimd=true;
};
/// Status per lane: 1 success, 2 error, 3 cancelled; errors are bounded detailed diagnostics.
struct BatchOutput {
    std::vector<Value> values;
    std::vector<std::uint8_t> status;
    std::vector<std::pair<std::size_t,Diagnostic>> errors;
    std::size_t failed=0,cancelled=0;
    std::size_t simdLanes=0;
};
/// Owned asynchronous work. Destruction requests cancellation, never joins; tasks own captures.
/// Scheduler must outlive finish; finish/drain before conflicting owner writes or teardown.
class BatchJob {
public:
    BatchJob()=default;
    ~BatchJob();
    BatchJob(BatchJob&&) noexcept=default;
    BatchJob& operator=(BatchJob&& other) noexcept;
    BatchJob(const BatchJob&)=delete;
    BatchJob& operator=(const BatchJob&)=delete;
    void cancel() noexcept;
    bool ready() const noexcept;
    /// Wait for this job's tasks only; returns once, with outputs in original bucket/lane order.
    Result<std::vector<BatchOutput>> finish();
private:
    struct Impl;
    std::shared_ptr<Impl> _impl;
    friend class BatchExecutor;
};
/// Uses an engine-owned scheduler, never creates a private thread pool.
class BatchExecutor {
public:
    /// Validate and move all captures before submitting; no borrowed caller arrays survive submission.
    static Result<BatchJob> submit(std::vector<BatchInput> inputs, ayt::task::ITaskScheduler& scheduler,
                                   BatchOptions options={});
    /// Synchronous reference with the same SoA validation and result layout.
    static Result<std::vector<BatchOutput>> scalar(std::vector<BatchInput> inputs, BatchOptions options={});
};
enum class ReadField { Effective, Current, Capacity, Base };
enum class BatchSemantics { Snapshot, Ordered };
/// state==SIZE_MAX denotes a literal. All live reads must be declared here.
struct Binding { std::size_t state=SIZE_MAX; StatId stat=0; ReadField field=ReadField::Effective; Value literal=0; };
struct Target { std::size_t state=0; StatId stat=0; MutationOp op=MutationOp::Decrease; };
struct SettlementRequest {
    std::uint64_t id=0;
    std::shared_ptr<const CalculationPlan> plan;
    CalculationContext context;
    std::vector<Binding> inputs;
    std::vector<Target> writes;
};
struct BatchSettlement { std::vector<Value> values; std::vector<std::vector<Settlement>> results; };
/// Owner-controlled atomic bank transaction. Ordered re-reads shadow state after each request;
/// Snapshot reads the original bank but still preserves ordered actual deduction attribution.
Result<BatchSettlement> settleBatch(std::span<StatSet*> bank,std::span<const SettlementRequest> requests,
                                    BatchSemantics semantics,std::span<const std::uint64_t> expectedVersions);
} // namespace ayt::stats
