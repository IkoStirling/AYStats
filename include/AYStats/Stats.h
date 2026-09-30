#pragma once
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayt::stats {
namespace detail { class SettlementAccess; }
using Value = std::int64_t;
using StatId = std::uint64_t;
inline constexpr Value RateScale = 10000;
enum class Error { Invalid, Missing, Duplicate, Cycle, Overflow, DivideByZero, Stale, Unsupported, Limit, Cancelled, Reentrant };
/// Owned diagnostic; path identifies the definition, stage or failing operation.
struct Diagnostic { Error code = Error::Invalid; std::string message; std::string path; };
template<class T> using Result = std::expected<T, Diagnostic>;
/// Stable FNV-1a identity. Schema compilation checks name/identity collisions.
StatId statId(std::string_view name) noexcept;
/// Checked signed addition; overflow is an error, never saturation or wraparound.
Result<Value> add(Value a, Value b);
/// Exact widened product followed by division rounded toward zero. No signed UB.
Result<Value> multiplyDivide(Value a, Value b, Value divisor);
enum class Shape { Scalar, Pool };
enum class Mode { Basic, Advanced };
enum class Lifetime { Persistent, Intermediate };
enum class CapacityPolicy { KeepCurrent, KeepRatio, KeepMissing };
enum class ZoneOp { Flat, PercentSum, FactorProduct, Minimum, Maximum, Override };
/// Ordered calculation zone; id is local to this definition, not an implicit Buff bucket.
struct Zone { std::uint32_t id = 0; ZoneOp op = ZoneOp::Flat; };
/// Immutable author definition. Numeric amounts are integer units, factors basis points.
struct Definition {
    std::string name;
    StatId id = 0;
    Shape shape = Shape::Scalar;
    Mode mode = Mode::Basic;
    Lifetime lifetime = Lifetime::Persistent;
    Value initial = 0;
    Value current = 0;
    Value capacity = 0;
    Value minimum = 0;
    Value maximum = std::numeric_limits<Value>::max();
    CapacityPolicy capacityPolicy = CapacityPolicy::KeepCurrent;
    StatId capacityFrom = 0;
    StatId baseFrom = 0;
    Value derivedFactor = RateScale;
    Value derivedAdd = 0;
    std::uint32_t stage = 0;
    std::vector<Zone> zones;
};
/// Compile names, references, ranges and acyclic capacity/base dependencies once.
class Schema {
public:
    static Result<std::shared_ptr<const Schema>> compile(std::vector<Definition> definitions);
    std::optional<std::size_t> index(StatId id) const noexcept;
    std::span<const Definition> definitions() const noexcept { return _definitions; }
private:
    std::vector<Definition> _definitions;
};
/// Full identity: source effect, rule, hit/layer, stage and target. Source alone is not a key.
struct Modifier {
    std::uint64_t handle = 0;
    std::uint64_t effect = 0;
    std::uint64_t rule = 0;
    std::uint64_t hit = 0;
    std::uint32_t layer = 0;
    StatId target = 0;
    std::uint32_t stage = 0;
    std::uint32_t zone = 0;
    Value value = 0;
    std::int32_t priority = 0;
    bool operator==(const Modifier&) const = default;
};
/// One row of a read-only inspection snapshot; effective and capacity are derived caches.
struct Row { Value base = 0, current = 0, capacity = 0, effective = 0; };
enum class MutationOp { SetBase, SetCurrent, Increase, Decrease, SetCapacity, Restore };
/// A settlement operation. Increase/Decrease require non-negative amount.
struct Mutation { StatId target = 0; MutationOp op = MutationOp::Decrease; Value amount = 0; };
/// Signed actual delta and non-negative excess; preserves per-operation attribution.
enum class ChangeField { Current, Base, Capacity };
struct Change { StatId target = 0; Value before = 0, after = 0, actual = 0, excess = 0; ChangeField field=ChangeField::Current; };
struct Settlement { std::uint64_t beforeVersion = 0, afterVersion = 0; std::vector<Change> changes; };

/// Single-owner persistent state; copy to obtain an independent snapshot, never mutate from workers.
class StatSet {
public:
    static Result<StatSet> create(std::shared_ptr<const Schema> schema);
    /// Read cached effective values; does not traverse effects or execute callbacks.
    Result<Row> read(StatId id) const;
    /// Atomic local transaction: validate/evaluate all operations before publishing state.
    Result<Settlement> commit(std::span<const Mutation> operations, std::uint64_t expectedVersion);
    /// Add one legal advanced modifier; duplicate identity and duplicate handles are errors.
    Result<void> insert(Modifier modifier);
    /// Remove by unique handle and recompute; no inverse arithmetic.
    Result<void> erase(std::uint64_t handle);
    /// Replace a modifier atomically; preserves identity/handle validation.
    Result<void> replace(Modifier modifier);
    /// Replace/remove an owned modifier group with a single final rebuild.
    /// Capacity policies see final contributions, never transient remove/reinsert capacity.
    /// owned snapshots must match exactly; unrelated same-effect contributions are preserved.
    Result<void> replaceOwnedModifiers(std::span<const Modifier> owned, std::span<const Modifier> modifiers);
    std::uint64_t version() const noexcept { return _version; }
    const std::shared_ptr<const Schema>& schema() const noexcept { return _schema; }
    std::span<const Modifier> modifiers() const noexcept { return _modifiers; }
    std::span<const Row> rows() const noexcept { return _rows; }
    /// Restore authoritative rows/modifiers into a new state; effective fields are rebuilt.
    static Result<StatSet> restore(std::shared_ptr<const Schema> schema, std::span<const Row> rows,
                                  std::span<const Modifier> modifiers, std::uint64_t version);
private:
    friend class detail::SettlementAccess;
    // Only an unpublished, exclusively owned batch shadow may use this non-atomic path.
    Result<Settlement> commitShadow(std::span<const Mutation> operations, std::uint64_t expectedVersion,
                                    std::vector<std::uint8_t>& rebuildScratch);
    Result<void> rebuild();
    Result<void> rebuild(std::vector<std::uint8_t>& visited);
    Result<Value> evaluate(std::size_t index, std::vector<std::uint8_t>& visited);
    Result<void> validateModifier(const Modifier& modifier, std::optional<std::uint64_t> replacing = {}) const;
    std::shared_ptr<const Schema> _schema;
    std::vector<Row> _rows;
    std::vector<Modifier> _modifiers;
    std::uint64_t _version = 1;
};
} // namespace ayt::stats
