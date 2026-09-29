#pragma once
#include <AYStats/Stats.h>
#include <functional>
#include <unordered_map>

namespace ayt::stats {
enum class Op { Input, Constant, Add, Subtract, Multiply, Divide, MultiplyRate, Minimum, Maximum, Clamp, Greater, Select, Table, Random, Extension };
/// Counter domain; hit and purpose isolate draws from worker scheduling and other targets.
struct CalculationContext {
    std::uint64_t seed=0, tick=0, calculation=0, hit=0, parent=0;
    std::uint32_t depth=0;
};
/// Owned author node; a/b/c are node ids. Input uses binding, Random uses purpose/value bound.
struct Node {
    std::uint32_t id=0;
    Op op=Op::Constant;
    std::uint32_t a=0,b=0,c=0,binding=0;
    Value value=0;
    std::vector<Value> table;
    std::string extension;
};
using Extension = std::function<Result<Value>(std::span<const Value>, const CalculationContext&)>;
/// Immutable after compile; callbacks must be pure/thread-safe and declare all input operands.
using Extensions = std::unordered_map<std::string,Extension>;
struct Instruction { Node node; std::size_t a=0,b=0,c=0; };
/// A validated, topologically sorted program; inputs and scratch are caller-owned contiguous arrays.
class CalculationPlan {
public:
    static Result<CalculationPlan> compile(std::vector<Node> nodes, std::uint32_t output,
                                         std::size_t inputCount, Extensions extensions = {});
    /// Pure evaluation; scratch length must equal/exceed instruction count. No per-node allocation.
    Result<Value> evaluate(std::span<const Value> inputs, const CalculationContext& context,
                           std::span<Value> scratch) const;
    std::span<const Instruction> instructions() const noexcept { return _instructions; }
    std::size_t inputCount() const noexcept { return _inputs; }
    std::size_t outputIndex() const noexcept { return _output; }
    bool hasExtensions() const noexcept { return !_extensions.empty(); }
private:
    std::vector<Instruction> _instructions;
    Extensions _extensions;
    std::size_t _inputs=0,_output=0;
};
/// Stateless draw from an explicit context/purpose/index. Not a shared mutable RNG.
std::uint64_t randomBits(const CalculationContext& context, std::uint64_t purpose, std::uint64_t draw=0) noexcept;
/// Trace is requested explicitly; retains each named stage value up to a hard limit.
struct CalculationTrace { Value result=0; std::vector<std::pair<std::uint32_t,Value>> stages; };
Result<CalculationTrace> trace(const CalculationPlan& plan, std::span<const Value> inputs,
                               const CalculationContext& context, std::size_t limit=4096);
} // namespace ayt::stats
