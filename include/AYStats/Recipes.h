#pragma once
#include <AYStats/Calculation.h>

namespace ayt::stats {
enum class AttributeOp { Increase, Decrease, Set };
enum class InputSource { Amount, SourceStat, TargetStat, Constant };
enum class InputField { Effective, Current, Base, Capacity };
/// Authored input provenance; plans themselves never look up entities.
struct RecipeInput {
    InputSource source = InputSource::Amount;
    StatId stat = 0;
    InputField field = InputField::Effective;
    Value constant = 0;
};
/// One immutable Stats recipe, compatible with exactly one target/operation.
struct Recipe {
    std::string name;
    StatId target = 0;
    AttributeOp operation = AttributeOp::Decrease;
    CalculationPlan plan;
    std::vector<RecipeInput> inputs;
    bool isDefault = false;
};
/// Compile configuration once. Explicit names never silently fall back to defaults.
class RecipeBook {
public:
    static Result<std::shared_ptr<const RecipeBook>> compile(std::vector<Recipe> recipes);
    Result<const Recipe*> select(StatId target, AttributeOp operation, std::string_view explicitName = {}) const;
    /// Read-only author order, used by versioned asset serialization and inspectors.
    std::span<const Recipe> recipes() const noexcept { return _recipes; }
private:
    std::vector<Recipe> _recipes;
    std::vector<std::size_t> _names,_defaults;
};
/// Pure sampling from explicit state views. Source is required only by SourceStat bindings.
Result<std::vector<Value>> sampleRecipe(const Recipe& recipe, Value amount,
                                      const StatSet& target, const StatSet* source = nullptr);
/// Translate a calculated output into a controlled mutation. Scalar operations change base.
/// Increase/Decrease require non-negative outputs; commit checks Set against authored ranges.
Result<Mutation> attributeMutation(const StatSet& state, StatId target, AttributeOp operation, Value calculated);
} // namespace ayt::stats
