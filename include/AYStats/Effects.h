#pragma once
#include <AYStats/Calculation.h>
#include <deque>

namespace ayt::stats {
enum class EffectPolicy { Independent, Refresh, Replace, Stack };
/// Half-open tick lifetime [applied, expires); tick UINT64_MAX means permanent.
struct Effect { std::uint64_t id=0,key=0,expires=UINT64_MAX; std::uint32_t maxStacks=1; std::vector<Modifier> modifiers; };
/// Single-owner timed modifier manager. State and effect lists publish together on success.
class EffectManager {
public:
    Result<void> apply(StatSet& set, Effect effect, EffectPolicy policy, std::uint64_t tick);
    Result<void> advance(StatSet& set, std::uint64_t tick);
    Result<void> remove(StatSet& set, std::uint64_t id);
    std::span<const Effect> effects() const noexcept { return _effects; }
    std::uint64_t tick() const noexcept { return _tick; }
    /// Restore manager ownership only after validating exact correspondence with saved modifiers.
    static Result<EffectManager> restore(const StatSet& set,std::vector<Effect> effects,std::uint64_t tick);
private:
    std::vector<Effect> _effects;
    std::uint64_t _tick=0;
};
/// Bounded FIFO reactions; parent ids identify causality, not mutation authorization.
class ReactionQueue {
public:
    explicit ReactionQueue(std::uint32_t maxDepth=16,std::size_t maxDerived=4096):_maxDepth(maxDepth),_limit(maxDerived){}
    /// Start one root domain; cannot reset while pending reactions exist.
    Result<void> begin();
    /// Enqueue explicit permitted child. A denied relation is a diagnostic, never a silent drop.
    Result<void> derive(const CalculationContext& parent, CalculationContext child, bool permitted);
    std::optional<CalculationContext> pop();
    std::size_t pending() const noexcept { return _pending.size(); }
private:
    std::deque<CalculationContext> _pending;
    std::vector<std::uint64_t> _ids;
    std::uint32_t _maxDepth;
    std::size_t _limit,_derived=0;
};
} // namespace ayt::stats
